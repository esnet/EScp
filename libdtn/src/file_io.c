#define _GNU_SOURCE

#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/mman.h>

#include <immintrin.h>
#include <stdatomic.h>

#include "file_io.h"
#include "args.h"

// FILE_STAT_COUNT is somewhat mis-named, it indirectly represents the
// maximum number of open files we can have in flight. As files are opened
// first and then sent, there is potential to have all slots filled with
// open files. As Linux has a soft limit of 1024 files, we set this number
// to something below that maximum. It must always be set below the configured
// FD limit.

#define FILE_STAT_COUNT 750
#define FILE_STAT_COUNT_HSZ 2048
#define FILE_STAT_COUNT_CC 16

#define FS_INIT        0xBAEBEEUL
#define FS_IO          (1UL << 31) // NOTE: These values set a limit to the
#define FS_COMPLETE    (1UL << 30) //       number of threads. It really
                                   //       should be 1UL<<62 & 1UL<<63
#define FS_MASK(A)     (A & (FILE_STAT_COUNT_HSZ-1))


/*
// The AVX routines are sort of stand-ins for atomically do something with
// a cacheline of memory. Should be replaced with something less platform
// dependant.
void memcpy_avx( void* dst, void* src ) {
          __m512i a;

          // b = _mm512_load_epi64 ( (void*) (((uint64_t) src) +  64) );
          a = _mm512_load_epi64 ( src );
          // _mm512_store_epi64( (void*) (((uint64_t) src) +  64), b );
          _mm512_store_epi64( dst, a );

}

void memset_avx( void* dst ) {
          __m512i a = {0};
          _mm512_store_epi64( dst, a );
}
*/

void memcpy_avx( void* dst, void* src ) {
          __m128i a,b,c,d;

          a = _mm_load_si128 ( src );
          b = _mm_load_si128 ( (void*) (((uint64_t) src) +  16) );
          c = _mm_load_si128 ( (void*) (((uint64_t) src) +  32) );
          d = _mm_load_si128 ( (void*) (((uint64_t) src) +  48) );
          _mm_store_si128( (void*) (((uint64_t) dst) +  48), d );
          _mm_store_si128( (void*) (((uint64_t) dst) +  32), c );
          _mm_store_si128( (void*) (((uint64_t) dst) +  16), b );
          _mm_store_si128( dst, a );

}

void memset_avx( void* dst ) {
          __m128i a = {0};
          _mm_store_si128( (void*) (((uint64_t) dst) +  48), a );
          _mm_store_si128( (void*) (((uint64_t) dst) +  32), a );
          _mm_store_si128( (void*) (((uint64_t) dst) +  16), a );
          _mm_store_si128( dst, a );
}

// Soft limit on file descriptors, must at least 50 less than FD limit, and
// much less than HSZ (which must be ^2 aligned).
struct file_stat_type file_stat[FILE_STAT_COUNT_HSZ]={0};

uint64_t file_claim __attribute__ ((aligned(64)));
uint64_t file_count __attribute__ ((aligned(64)));
uint64_t file_head  __attribute__ ((aligned(64)));
uint64_t file_tail  __attribute__ ((aligned(64)));

struct file_stat_type file_activefile[THREAD_COUNT] = {0};

static inline uint64_t xorshift64s(uint64_t* x) {
  *x ^= *x >> 12; // a
  *x ^= *x << 25; // b
  *x ^= *x >> 27; // c
  return *x * 0x2545F4914F6CDD1DUL;
}

void file_incrementfilecount() {
  uint64_t slot, cur, orig;
  int i;
  cur = orig = atomic_load( &file_count );


  while (1) {
    slot = ++cur;

    for (i=0; i<FILE_STAT_COUNT_CC; i++) {
      slot = xorshift64s(&slot);
      if ( atomic_load( &file_stat[FS_MASK(slot)].file_no ) == cur) {
        break;
      }
    }

    if (i >= FILE_STAT_COUNT_CC) {
      break;
    }

  }

  cur -= 1;

  if (orig != cur) {
    if (atomic_fetch_add( &file_count, cur - orig ))
      DBG("Increment file_count from %ld to %ld", orig, cur);
  }
}

struct file_stat_type* file_addfile( uint64_t fileno, int fd, uint32_t crc,
                                     int64_t file_sz ) {
  struct file_stat_type fs = {0};
  int i;
  uint64_t zero = 0;

  uint64_t slot = fileno;
  slot = xorshift64s(&slot);

  uint64_t fc, ft;


  while (1) {
    // If Queue full, idle
    fc = atomic_load( &file_claim );
    ft = atomic_load( &file_tail );

    if ( (fc-ft) < FILE_STAT_COUNT )
      break;

    if ( (fileno - ft) < (FILE_STAT_COUNT+50) )
      break;

    ESCP_DELAY(10);
  }

  for (i=0; i<FILE_STAT_COUNT_CC; i++) {
    zero=0;
    if ( atomic_compare_exchange_weak( &file_stat[FS_MASK(slot)].state, &zero, 0xBedFaceUL ) ) {
      break;
    }
    slot = xorshift64s(&slot);
  }

  VRFY( i<FILE_STAT_COUNT_CC, "Hash table collision count exceeded. Please report this error.");

  fs.state = FS_INIT;
  fs.fd = fd;
  fs.file_no = fileno;
  fs.bytes = file_sz;
  fs.position = FS_MASK(slot);
  fs.poison = 0xC0DAB1E;

  memcpy_avx( &file_stat[FS_MASK(slot)], &fs );
  atomic_fetch_add( &file_claim, 1 );

  DBG("file_addfile fn=%ld, fd=%d crc=%X slot=%ld cc=%d",
      fileno, fd, crc, FS_MASK(slot), i);

  file_incrementfilecount();

  return( &file_stat[FS_MASK(slot)] );
}

struct file_stat_type* file_wait( uint64_t fileno, struct file_stat_type* test_fs, int id ) {

  int i;
  uint64_t fc, slot, fs_init;

  // DBG("file_wait start fn=%ld", fileno);

  while (1) {
    fc = atomic_load( &file_count );
    if (fileno <= fc) {
      break;;
    }
    ESCP_DELAY(10);
  }

  // DBG("file_wait ready on fn=%ld", fileno);

  slot = fileno;

  for (i=0; i<FILE_STAT_COUNT_CC; i++) {
    slot = xorshift64s(&slot);

    memcpy_avx( test_fs, &file_stat[FS_MASK(slot)] );
    if (test_fs->file_no != fileno)
      continue;

    fs_init = FS_INIT;
    if ( (test_fs->state == FS_INIT) && atomic_compare_exchange_weak(
      &file_stat[FS_MASK(slot)].state, &fs_init, FS_COMPLETE|FS_IO) )
    {
      DBG("[%2d] NEW IOW on fn=%ld, slot=%ld", id, test_fs->file_no, FS_MASK(slot));
      return &file_stat[FS_MASK(slot)]; // Fist worker on file
    }

    if (test_fs->state & FS_IO) {
      DBG("[%2d] ADD writer to fn=%ld", id, test_fs->file_no);
      return &file_stat[FS_MASK(slot)]; // Add worker to file
    }
  }

  // VRFY(0, "[%2d] Couldn't convert fileno", id);
  NFO("[%2d] Couldn't convert fileno, trying again.", id);
  return file_wait( fileno, test_fs, id );
}


struct file_stat_type* file_next( int id, struct file_stat_type* test_fs ) {

  // Generic function to fetch the next file, may return FD to multiple
  // threads depending on work load / incomming file stream.
  //
  DBV("[%2d] Enter file_next", id);

  uint64_t fc,fh,slot;
  int i,j,k=0;;

  while (1) {
    fc = atomic_load( &file_count );
    fh = atomic_load( &file_head );

    // First try to attach to a new file

    if ( (fc-fh) > THREAD_COUNT ) {
      // No worries about blasting past file_count, just grab and go
      fh = atomic_fetch_add( &file_head, 1 );
      break;
    }

    if ( (fc-fh) > 0 ) {
      // Increment carefully
      if ( atomic_compare_exchange_weak( &file_head, &fh, fh+1) )
        break;
      continue;
    }

    // No new files, see if we can attach to an existing file
    for (i=0; i< THREAD_COUNT; i++) {
      // Should iterate on actual thread count instead of max threads
      j = atomic_load( &file_activefile[i].position );
      if (j) {
        uint64_t st;
        memcpy_avx( test_fs, &file_stat[j] );
        st = test_fs->state;
        if ( (test_fs->state & FS_IO) && atomic_compare_exchange_weak(
          &file_stat[j].state, &st, st| (1<<id) ) ) {
          return &file_stat[j];
        }
      }
    }

    if ((k++&0xffff) == 0xffff) {
      // We use CAS weak, which can fail sometimes. If it did fail, we
      // need to repeat our check, which we do below.
      file_incrementfilecount();
    }

    // Got nothing, wait and try again later.
    ESCP_DELAY(1);
  }

  // We got a file_no, now we need to translate it into a slot.

  slot = ++fh;

  while (1) {

    slot = fh;
    for (i=0; i<FILE_STAT_COUNT_CC; i++) {
      uint64_t fs_init;
      slot = xorshift64s(&slot);

      memcpy_avx( test_fs, &file_stat[FS_MASK(slot)] );
      if (test_fs->file_no != fh) {
        continue; // This indicates we had a hash collision and need to fetch the next item
      }

      fs_init = FS_INIT;
      if ( (test_fs->state == FS_INIT) && atomic_compare_exchange_weak(
        &file_stat[FS_MASK(slot)].state, &fs_init, FS_COMPLETE|FS_IO|(1<<id) ) )
      {
        DBG("[%2d] NEW IOW on fn=%ld, slot=%ld", id, test_fs->file_no, FS_MASK(slot));
        memcpy_avx( &file_activefile[id], test_fs );
        return &file_stat[FS_MASK(slot)]; // Fist worker on file
      } else {
        NFO("[%2d] Failed to convert fn=%ld, slot=%ld", id, test_fs->file_no, FS_MASK(slot));
      }
    }
    usleep(1);
  }

  VRFY( 0, "[%2d] Error claiming file fn=%ld", id, fh );
  return 0;
}

uint64_t  file_iow_remove( struct file_stat_type* fs, int id ) {
  DBV("[%2d] Release interest in file: fn=%ld state=%lX fd=%d", id, fs->file_no, fs->state, fs->fd);

  // uint64_t res = __sync_and_and_fetch( &fs->state, ~((1UL << id) | FS_IO) );
  uint64_t res = atomic_fetch_and( &fs->state, ~((1UL << id) | FS_IO) );
  res &= ~((1UL << id) | FS_IO) ;

  if (res == FS_COMPLETE) {
    atomic_fetch_add( &file_tail, 1 );
  }

  return res;
}

void file_incrementtail() {
  atomic_fetch_add( &file_tail, 1 );
}



int file_get_activeport( void* args_raw ) {
  int res;
  struct dtn_args* args = (struct dtn_args*) args_raw;

  while ( !(res=atomic_load(&args->active_port)) ) {
    ESCP_DELAY(10);
  }

  return res;
}


int32_t file_hash( void* block, int sz, int seed ) {
  uint32_t *block_ptr = (uint32_t*) block;
  uint64_t hash = seed;
  hash = xorshift64s(&hash);
  int i=0;

  if (sz%4) {
    for ( i=0; i < (4-(sz%4)); i++ ) {
      ((uint8_t*)block_ptr)[sz+i] = 0;
    }
  }

  for( i=0; i<(sz+3)/4; i++ ) {
    hash = __builtin_ia32_crc32si( block_ptr[i], hash );
  }

  return hash;
}

struct file_object* file_memoryinit( void* arg, int id ) {
  struct dtn_args* dtn = arg;
  struct file_object* fob = aligned_alloc( 64, sizeof(struct file_object) );
  struct file_object f = {0};

  memset( fob, 0, sizeof(struct file_object) );

  f.io_type = dtn->io_engine;
  f.QD = dtn->QD;
  f.blk_sz = dtn->block;
  f.io_flags = dtn->flags;
  f.thread_count = dtn->thread_count;
  f.args = &dtn->io_engine_name[5];
  f.id = id;
  f.compression = dtn->compression;
  f.hugepages = dtn->hugepages;
  f.do_hash = dtn->do_hash;

  switch (f.io_type) {
#ifdef __ENGINE_POSIX__
    case FIIO_POSIX:
      file_posixinit( &f );
      break;
#endif
#ifdef __ENGINE_URING__
    case FIIO_URING:
      file_uringinit( &f );
      break;
#endif
#ifdef __ENGINE_DUMMY__
    case FIIO_DUMMY:
      file_dummyinit( &f );
      break;
#endif
/*
    case FIIO_SHMEM:
      shmem_init( &f );
      break;
*/
    default:
      VRFY( 0, "No matching engine for '%x'",
               fob->io_type );
  }

  memcpy ( fob, &f, sizeof(struct file_object) );

  return fob;

}

void file_prng( void* buf, int sz ) {
  int i=0, offset=0;
  uint64_t* b = buf;
  static __thread uint64_t s=0;

  if ( s == 0 )
    file_randrd( &s, 8 );

  while ( sz > 1023 ) {
    // Compiler optimization hint
    for (i=0; i<128; i++)
      b[offset+i] = xorshift64s(&s);
    offset += 128;
    sz -= 1024;
  }

  for ( i=0; i < ((sz+7)/8); i++ ) {
      b[offset+i] = xorshift64s(&s);
  }
}

void file_randrd( void* buf, int count ) {
    int fd = open("/dev/urandom", O_RDONLY);
    VRFY( fd > 0, );
    VRFY( count == read(fd, buf, count), );
    close(fd);
}

/* Note: These queues are non-blocking; If they are full, they will overrun
         data.  We try to mitigate the effects of an overrun by by copying
         the result to char[] msg; but that is obviously imperfect.

         The code does detect when the queue has been overrun, but it
         doesn't do anything with that information.

         While you can make the queue larger, it doesn't really help if
         the consumers are slower than the producers.

         The best approach if you are having overwritten messages is
         to have multiple log writers; but for that to work, you need
         to update the cursor to be a real tail using atomic operations.
  */

char* dtn_log_getnext() {
  static __thread int64_t cursor=0;
  static __thread char msg[ESCP_MSG_SZ];

  int64_t head = atomic_load( &ESCP_DTN_ARGS->debug_count );
  char* ptr;

  if ( cursor >= head )
    return NULL; // Ring Buffer Empty

  if ( (head > ESCP_MSG_COUNT) &&
      ((head - ESCP_MSG_COUNT) > cursor ) ) {

    // head has advanced past implicit tail
    cursor = head - ESCP_MSG_COUNT;
  }

  ptr = (char*) (((cursor % ESCP_MSG_COUNT)*ESCP_MSG_SZ)
                   + ESCP_DTN_ARGS->debug_buf);

  memcpy( msg, ptr, ESCP_MSG_SZ );
  cursor++;
  return msg;
}

char* dtn_err_getnext() {
  static __thread int64_t cursor=0;
  static __thread char msg[ESCP_MSG_SZ];

  int64_t head = atomic_load( &ESCP_DTN_ARGS->msg_count );
  char* ptr;

  if ( cursor >= head )
    return NULL; // Ring Buffer Empty

  if ( (head > ESCP_MSG_COUNT) &&
      ((head - ESCP_MSG_COUNT) > cursor ) ) {

    // head has advanced past implicit tail
    cursor = head - ESCP_MSG_COUNT;
  }

  ptr = (char*) (((cursor % ESCP_MSG_COUNT)*ESCP_MSG_SZ)
                   + ESCP_DTN_ARGS->msg_buf);

  memcpy( msg, ptr, ESCP_MSG_SZ );
  cursor++;
  return msg;
}



