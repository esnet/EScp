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

#include "file_io.h"
#include "args.h"

// FILE_STAT_COUNT is somewhat mis-named, it indirectly represents the
// maximum number of open files we can have in flight. As files are opened
// first and then sent, there is potential to have all slots filled with
// open files. As Linux has a soft limit of 1024 files, we set this number
// to something below that maximum. It must always be set below the configured
// FD limit.

#define FILE_STAT_COUNT 800
#define FS_INIT        0x1cafc886
#define FS_IO          (1UL << 63)
#define FS_COMPLETE    (1UL << 62)
#define FS_RECYCLE     0xc15ca978

struct file_stat_type file_stat[FILE_STAT_COUNT]={0};

uint64_t rand_seed=0;      // for Dummy IO
uint64_t bytes_written=0;  // for Dummy IO

bool file_persist_state = false;

uint64_t file_count = 0;
uint64_t file_completed = 0;
uint64_t file_wanted __attribute__ ((aligned(64))) = 0;

uint64_t file_fileno_next  __attribute__ ((aligned(64))) = 0;

struct file_stat_type* file_addfile(uint64_t fileno, int fd, uint32_t crc, int64_t file_sz ) {
  struct file_stat_type fs= {0};
  int i=fileno % FILE_STAT_COUNT;
  uint64_t iterations=0;

  if ( fileno == ~0UL ) {
    __sync_add_and_fetch( &file_fileno_next, 1 );
    i=0;
  }

  if (!fileno) {
    fileno = __sync_add_and_fetch( &file_fileno_next, 1 );
  }

  DBV("Entered file_addfile fn=%ld, fd=%d crc=%X",
      fileno, fd, crc);

  while (1) {

    if ( (iterations++ % FILE_STAT_COUNT) == (FILE_STAT_COUNT-1) )
      usleep(1);

    if ( file_stat[i].fd != 0 ) {
      i = (i+1) % FILE_STAT_COUNT;
      continue;
    }

    if ( __sync_val_compare_and_swap( &file_stat[i].state, 0, 0xBedFace0 ) ) {
      i = (i+1) % FILE_STAT_COUNT;
      continue;
    }

    // Got an empty file_stat structure

    fs.state = FS_INIT;
    fs.fd = fd;
    // fs.given_crc = crc;
    fs.poison = 0x4BADC01F;
    fs.file_no = fileno;
    fs.bytes = file_sz;

    break;
  }

  memcpy( &file_stat[i], &fs, sizeof(struct file_stat_type) );
  return( &file_stat[i] );
}

struct file_stat_type* file_wait( uint64_t fileno ) {
  DBV("Enter file_wait with fn=%ld", fileno);

  struct file_stat_type test_fs;
  int64_t test_fn;
  int i, k;

  while (1) {
    k = FILE_STAT_COUNT;
    test_fn = __sync_fetch_and_add(&file_fileno_next, 0);

    if (test_fn && (test_fn < FILE_STAT_COUNT))
      k = test_fn;

    for (i=0; i<=k; i++) {
      memcpy( &test_fs, &file_stat[i], sizeof(struct file_stat_type) );

      if (test_fs.file_no != fileno)
        continue;

      if ( (test_fs.state == FS_INIT) && (__sync_val_compare_and_swap(
        &file_stat[i].state, FS_INIT, FS_COMPLETE|FS_IO) == FS_INIT ) )
      {
        DBV("NEW writer on %lx", file_stat[i].file_no);
        return &file_stat[i]; // Fist worker on file
      }

      if (test_fs.state & FS_IO) {
        DBV("ADD writer on %lx", file_stat[i].file_no);
        return &file_stat[i]; // Added worker to file
      }
    }

    usleep(10);
  }
}


struct file_stat_type* file_next( int id ) {

  // Generic function to fetch the next file, may return FD to multiple
  // threads depending on work load / incomming file stream.
  //
  DBV("[%2d] Enter file_next", id);

  struct file_stat_type test_fs;
  int64_t test_fn;
  int i, j, k, did_iteration=0, do_exit=0, active_file;


  while (1) {
    k = FILE_STAT_COUNT;
    test_fn = __sync_fetch_and_add(&file_fileno_next, 0);
    if (test_fn && (test_fn < FILE_STAT_COUNT))
      k = test_fn;
    active_file=0;

    for (i=1; i<=k; i++) {
      j = (FILE_STAT_COUNT + test_fn - i) % FILE_STAT_COUNT;
      memcpy( &test_fs, &file_stat[j], sizeof(struct file_stat_type) );

      if (test_fs.file_no == ~0UL) {
        do_exit=1;
        continue;
      }

      if (test_fs.fd == 0)
        continue;

      active_file=1;
      if ( (test_fs.state == FS_INIT) && (__sync_val_compare_and_swap(
        &file_stat[j].state, FS_INIT, FS_COMPLETE|FS_IO|(1<<id) ) == FS_INIT ) )
      {
        DBV("[%2d] NEW IOW on fn=%ld, slot=%d/%016lx", id, file_stat[j].file_no, j, (uint64_t) &file_stat[j]);
        return &file_stat[j]; // Fist worker on file
      }

      uint64_t st = test_fs.state;
      if ( (test_fs.state & FS_IO) && (__sync_val_compare_and_swap(
        &file_stat[j].state, st, st| (1<<id) ) == st) ) {

        DBV("[%2d] ADD IOW on %lx", id, file_stat[j].file_no);
        return &file_stat[j]; // Added worker to file

      }

    }

    if ( !active_file  && do_exit ) {
      DBV("file_next: exit criteria is reached");
      return NULL;
    }

    if (!did_iteration) {
      did_iteration=1;
      continue;
    }
    usleep(1000);
  }

}

uint64_t  file_iow_remove( struct file_stat_type* fs, int id ) {
  DBV("[%2d] Release interest in file: fn=%ld state=%lX fd=%d", id, fs->file_no, fs->state, fs->fd);
  return __sync_and_and_fetch( &fs->state, ~((1UL << id) | FS_IO) );
}


int file_get_activeport( void* args_raw ) {
  int res;
  struct dtn_args* args = (struct dtn_args*) args_raw;

  while ( !(res= __sync_add_and_fetch(&args->active_port, 0) ) ) {
    usleep(1000);
  }

  return res;
}

static inline uint64_t xorshift64s(uint64_t* x) {
  *x ^= *x >> 12; // a
  *x ^= *x << 25; // b
  *x ^= *x >> 27; // c
  return *x * 0x2545F4914F6CDD1DUL;
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


void* file_ioworker( void* arg ) {
  struct file_stat_type* fs=0;
  struct dtn_args* dtn = arg;
  struct file_object* fob;
  int id = __sync_fetch_and_add(&dtn->thread_id, 1);
  uint64_t s = rand_seed;

  uint8_t* buffer;
  uint64_t res;
  void* token;
  int32_t sz;
  uint32_t crc=0;

  affinity_set( dtn );
  fob = file_memoryinit( dtn, id );
  (void*) __sync_val_compare_and_swap ( &dtn->fob, 0, (void*) fob );

  if (s) {
    // Seed S differently for each thread
    s ^= 0x94D049BB133111EBUL * id;
  }

  fob->id = id;
  DBV("[%2d] Start worker", id);

  while (1) {
    uint64_t offset;
    int64_t io_sz=1;

    if (!fs) {

      fs = file_next( fob->id );

      if (!fs) {
        DBV("[%2d] no more work, exit work loop", id);
        break;
      } else {
        DBV("[%2d] IOW attached to fn: %ld, fd: %d", id, fs->file_no, fs->fd);
      }
      crc= 0;

    }


    while ( (token=fob->fetch(fob)) ) {

      offset = __sync_fetch_and_add( &fs->block_offset, 1 );
      offset *= dtn->block;
      fob->set(token, FOB_OFFSET, offset);
      fob->set(token, FOB_FD, fs->fd);

      if ( dtn->flags & O_WRONLY ) {
        io_sz = dtn->disable_io - offset;

        if ( io_sz > dtn->block )
          io_sz = dtn->block;
      } else {
        io_sz = dtn->block;
      }

      if (io_sz <= 0) {
        fob->set( token, FOB_SZ, 0 );
        break;
      }

      fob->set( token, FOB_SZ, io_sz );

      buffer = fob->get(token, FOB_BUF );

      if (s) {
        int i,j;
        for (i=0; i<dtn->block; i+=4096)
          for (j=0; j<4096; j+=8)
            ((uint64_t*)(buffer+i+j))[0] = xorshift64s(&s);
      }

    }

    sz = dtn->block;
    if ( (token=fob->submit(fob, &sz, &res)) ) {
      VRFY ( sz >= 0, "IO Error");

      if (sz<1) {

        XLOG("file_ioworker: queue_flush");

        while ( (token = fob->submit(fob, &sz, &res)) ) {
          fob->complete(fob, token);
        }

        XLOG("file_ioworker: flush complete");

        if (file_iow_remove( fs, id ) == FS_COMPLETE) {
          DBG("[%2d] Close on file_no: %ld, fd=%d", id, fs->file_no, fs->fd);
          fob->close( fob );
          bzero( fs, sizeof( struct file_stat_type ) );
        }

        fs= 0;
        continue;
      }

      offset = (uint64_t) fob->get( token, FOB_OFFSET );
      buffer = fob->get( token, FOB_BUF );

      crc ^= file_hash( buffer, sz, (offset+dtn->block-1LL)/dtn->block );
      fob->complete(fob, token);

      __sync_fetch_and_add( &fs->bytes_total, sz );
    }
  }

  while ( (token=fob->submit(fob, &sz, &res)) ) {
    fob->complete(fob, token);
    VRFY ( sz >= 0 , "IO Error");
    __sync_fetch_and_add( &bytes_written, sz );
  }

  DBV("[%2d] worker finished", id);
  return 0;
}

void file_randrd( void* buf, int count ) {
    int fd = open("/dev/urandom", O_RDONLY);
    VRFY( fd > 0, );
    VRFY( count == read(fd, buf, count), );
    close(fd);
}

pthread_t file_iotest_thread[THREAD_COUNT];
uint64_t file_iotest_start_time;
int file_iotest_tc=0;

void file_iotest( void* args ) {
  int i=0;
  struct dtn_args* dtn = args;
  pthread_attr_t attr;
  pthread_attr_init(&attr);

  struct timeval t;

  if (dtn->disable_io > 11) {
    file_randrd( &rand_seed, 8 );
  }

  gettimeofday( &t, NULL );
  file_iotest_start_time = t.tv_sec * 1000000 + t.tv_usec;

  for ( i=0; i< dtn->thread_count; i++ ) {
    file_iotest_tc++;
    if ( pthread_create( &file_iotest_thread[i], &attr, &file_ioworker, dtn ) ) {
      perror("pthread");
      exit(-1);
    }
  }
}

void file_iotest_finish() {
  int i;

  for ( i=0; i< file_iotest_tc; i++ )
    pthread_join( file_iotest_thread[i], NULL );

  /*
  uint64_t now_time;
  float duration;
  struct timeval t;

  gettimeofday( &t, NULL );
  now_time = t.tv_sec * 1000000 + t.tv_usec ;
  duration = (now_time - file_iotest_start_time) / 1000000.0;

  NFO(
      "%siB, Elapsed= %.2fs, %sbit/s %siB/s",
      human_write(bytes_written, true),
      duration, human_write(bytes_written/duration,false),
      human_write(bytes_written/duration,true)
  );

  MMSG("Bytes: %sbit/s %s",
       human_write(bytes_written/duration,false),
       human_write(bytes_written,true)  );

  */
}

int64_t file_stat_getbytes( void *file_object, int fd ) {
  struct file_object* fob = file_object;
  struct stat st;
  int res;

  res = fob->fstat( fd, &st );

  if (res)
    return -1;

  return st.st_size;
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

  int64_t head = __sync_fetch_and_add( &ESCP_DTN_ARGS->debug_count, 0 );
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

  int64_t head = __sync_fetch_and_add( &ESCP_DTN_ARGS->msg_count, 0 );
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



