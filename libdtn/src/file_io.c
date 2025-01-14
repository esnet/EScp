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

#define FILE_STAT_COUNT 850
#define FILE_STAT_COUNT_HSZ 2048
#define FILE_STAT_COUNT_CC 12

#define FS_INIT        0xBAEBEEUL
#define FS_IO          (1UL << 31) // NOTE: These values set a limit to the
#define FS_COMPLETE    (1UL << 30) //       number of threads. It really
                                   //       should be 1UL<<62 & 1UL<<63
#define FS_MASK(A)     (((uint64_t)A) % FILE_STAT_COUNT_HSZ)

#ifdef __AVX512BW__
#warning AVX512 is Experimental; standard is -march=sandy
#include <immintrin.h>

void memcpy_avx( void* dst, void* src ) {
          __m512i a;

          a = _mm512_load_epi64 ( src );
          _mm512_store_epi64( dst, a );

}

void memset_avx( void* dst ) {
          __m512i a = {0};
          _mm512_store_epi64( dst, a );
}

int memcmp_zero( void* dst, uint64_t sz ) {
  __m256i a,b;
  __m256i zero = {0};

  _mm256_testz_si256
  uint64_t offset;

  for ( offset = 0; offset < sz; offset += 64 ) {
    uint64_t* src = (uint64_t*)(((uint64_t) dst) + offset);

    a = _mm_load_si256 ( (void*) src );
    b = _mm_load_si256 ( (void*) (((uint64_t) src) +  32) );

    res  = _mm256_testz_si256 (a, 0);
    res |= _mm256_testz_si256 (b, 0);

    if (res)
      return 0;
  }

  return 1;

}



#elif __AVX__
#include <emmintrin.h>
#include <smmintrin.h>
int memcmp_zero( void* dst, uint64_t sz ) {

  __m128i a,b,c,d;

  uint64_t offset;

  for ( offset = 0; offset < sz; offset += 64 ) {
    uint64_t* src = (uint64_t*)(((uint64_t) dst) + offset);

    a = _mm_load_si128 ( (void*) src );
    b = _mm_load_si128 ( (void*) (((uint64_t) src) +  16) );
    c = _mm_load_si128 ( (void*) (((uint64_t) src) +  32) );
    d = _mm_load_si128 ( (void*) (((uint64_t) src) +  48) );

    // on paper: test_all_ones has better throughput than test all zeroes

    int res = _mm_test_all_ones(~a);
    res &=  _mm_test_all_ones(~b);
    res &=  _mm_test_all_ones(~c);
    res &=  _mm_test_all_ones(~d);

    if (!res)
      return 1;
  }

  return 0; // If all ZERO

}

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


#else
#warning Compiling with slower? memcmp_zero

inline int memcmp_zero( void* dst, uint64_t sz ) {
  char zero[64] = {0};
  for ( i=0; i<sz; i+=64 )
    int res;
    if ((res=memcmp( (void*)(*((uint64*)dst) + i), zero )))
      return res;
  return 0;
}

void memcpy_avx( void* dst, void* src ) {
  memcpy( dst, src, 64 );
}

void memset_avx( void* src ) {
  memset( src, 0, 64 );
}

#endif




// Soft limit on file descriptors, must at least 50 less than FD limit, and
// much less than HSZ (which must be ^2 aligned).
struct file_stat_type file_stat[FILE_STAT_COUNT_HSZ]={0};

uint64_t file_claim __attribute__ ((aligned(64))) = 0;
uint64_t file_count __attribute__ ((aligned(64))) = 0;
uint64_t file_head  __attribute__ ((aligned(64))) = 0;
uint64_t file_tail  __attribute__ ((aligned(64))) = 0;

uint64_t transfer_complete __attribute__ ((aligned(64))) = 0;

struct file_stat_type file_activefile[THREAD_COUNT] = {0};

static inline uint64_t xorshift64s(uint64_t* x) {
  *x ^= *x >> 12; // a
  *x ^= *x << 25; // b
  *x ^= *x >> 27; // c
  return *x * 0x2545F4914F6CDD1DUL;
}

uint64_t xorshift64r(uint64_t x) {
  x ^= x >> 12; // a
  x ^= x << 25; // b
  x ^= x >> 27; // c
  return x * 0x2545F4914F6CDD1DUL;
}

void file_completetransfer() {
    NFO("file_completetransfer() is called");
    atomic_fetch_add( &transfer_complete, 1 );
}

struct file_stat_type* file_addfile( uint64_t fileno, int fd ) {

  struct file_stat_type fs __attribute__ ((aligned(64))) = {0};
  uint64_t zero=0, slot, fc, ft, hash=fileno;
  int i;

  hash = xorshift64s(&hash);
  fc = atomic_load( &file_claim );
  ft = atomic_load( &file_tail );

  if ( ((fc-ft) >= FILE_STAT_COUNT) || ((fileno-ft) >= (FILE_STAT_COUNT+50)) )
    return NULL;

  for (i=0; i<FILE_STAT_COUNT_CC; i++) {
    slot = FS_MASK(hash);
    VRFY( zero == 0, "Zero != 0!, %ld", fileno);
    if ( atomic_compare_exchange_strong( &file_stat[slot].state, &zero, 0xBedFaceUL ) ) {
      break;
    }
    hash = xorshift64s(&hash);
  }

  VRFY(i<FILE_STAT_COUNT_CC, "Hash table collision count exceeded. Bug Report Pls!.");

  fs.state = FS_INIT;
  fs.fd = fd;
  fs.file_no = fileno;
  fs.position = slot;
  fs.poison = 0xC0DAB1E;

  memcpy_avx( &file_stat[slot], &fs );
  atomic_fetch_add( &file_claim, 1 );

  VRFY( atomic_load( & file_stat[slot].state ) == FS_INIT, "FS_INIT?" );
  VRFY( sizeof( file_stat[0] ) == 64, "Bleh!" );

  DBG("file_addfile fn=%ld, fd=%d slot=%d cc=%d",
      fs.file_no, fs.fd, fs.position, i );

  atomic_fetch_add( &file_count, 1 );

  return( &file_stat[slot] );
}

struct file_stat_type* file_getstats( uint64_t fileno ) {
  uint64_t slot=fileno;

  for (int i=0; i<FILE_STAT_COUNT_CC; i++) {
    slot = xorshift64s(&slot);

    if ( fileno != atomic_load(&file_stat[FS_MASK(slot)].file_no) )
      continue;

    return &file_stat[FS_MASK(slot)];
  }

  return 0;
}


struct file_stat_type* file_wait( uint64_t fileno, struct file_stat_type* test_fs, int id ) {

  int i;
  uint64_t slot, fs_init;

  float delay = 3;

  // DBG("file_wait start fn=%ld", fileno);

  while (1) {
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

    ESCP_DELAY((int)delay);
    delay *= 1.121743;
  }
}


struct file_stat_type* file_next( int id, struct file_stat_type* test_fs ) {

  // Used by sender to fetch next availble file and/or attach to existing file
  DBV("[%2d] Enter file_next", id);

  int64_t fc,fh,slot;
  int i,j=0;
  float delay = 1;

  while (1) {
    fc = atomic_load( &file_count ); // In order increment of available files
    fh = atomic_load( &file_head );  // file claimed by file_next

    // First try to attach to a new file
    if ( (fc-fh) > THREAD_COUNT ) {
      // There are many available files, anything we grab is fine
      fh = atomic_fetch_add( &file_head, 1 ); // fh = old value
      break;
    }

    if ( (fc-fh) > 0 ) {
      // Between 1 and THREAD_COUNT, grab next available file
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

    if ( atomic_load( &transfer_complete ) ) {
      DBG("[%2d] file_next got transfer_complete flag", id );
      return 0;
    }

    ESCP_DELAY( (int)delay ); // No work found, try again
    delay *= 1.057329;
  }

  // We got a file_no, now we need to translate it into a slot.

  slot = ++fh;
  DBG("[%2d] Trying to claim slot fn=%zd, %zd", id, slot, fc);

  while (1) {
    // While unlikely, it is possible that our file_claim slot did not have
    // *this* file assigned to it yet, so we loop until it exists.

    slot = fh;
    for (i=0; i<FILE_STAT_COUNT_CC; i++) {
      uint64_t fs_init;
      slot = xorshift64s( (uint64_t*) &slot);

      memcpy_avx( test_fs, &file_stat[FS_MASK(slot)] );
      if (test_fs->file_no != fh) {
        continue; // Possible hash collision; Try next item
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

    ESCP_DELAY( (int) delay ); // No work found, try again
    delay *= 1.057329;
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
  struct file_object* fob = aligned_alloc( 64, (sizeof(struct file_object) + 0x3f) & 0xffff40 );
  struct file_object f = {0};

  memset( fob, 0, sizeof(struct file_object) );

  f.io_type = dtn->io_engine;
  f.QD          = dtn->QD;
  f.blk_sz      = dtn->block;
  f.io_flags    = dtn->flags;
  f.thread_count= dtn->thread_count;
  f.args        = &dtn->io_engine_name[5];
  f.id          = id;
  f.compression = dtn->compression;
  f.sparse      = dtn->sparse;
  f.hugepages   = dtn->hugepages;
  f.do_hash     = dtn->do_hash;

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
#ifdef __ENGINE_SHMEM__
    case FIIO_SHMEM:
      file_shmeminit( &f );
      break;
#endif
    default:
      VRFY( 0, "No matching engine for '%x'",
               fob->io_type );
  }

  memcpy ( fob, &f, sizeof(struct file_object) );

  return fob;

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
