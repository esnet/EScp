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
#define FS_INIT        0xBAEBEEUL
#define FS_IO          (1UL << 31)
#define FS_COMPLETE    (1UL << 30)

struct file_stat_type file_stat[FILE_STAT_COUNT]={0};

struct file_stat_type* file_addfile( uint64_t fileno, int fd, uint32_t crc,
                                     int64_t file_sz ) {
  struct file_stat_type fs;
  int i=fileno % FILE_STAT_COUNT;
  uint64_t iterations=0;

  DBV("Entered file_addfile fn=%ld, fd=%d crc=%X",
      fileno, fd, crc);

  VRFY(fileno, "Assert: File number!=0");

  while (1) {

    if ( (iterations++ % FILE_STAT_COUNT) == (FILE_STAT_COUNT-1) )
      usleep(10000);

    memcpy ( &fs, &file_stat[i], sizeof(struct file_stat_type) );

    if ( fs.state != 0UL ) {
      i = (i+1) % FILE_STAT_COUNT;
      continue;
    }

    if ( __sync_val_compare_and_swap( &file_stat[i].state, 0, 0xBedFaceUL ) ) {
      i = (i+1) % FILE_STAT_COUNT;
      continue;
    }

    memset ( &fs, 0, sizeof(struct file_stat_type) );

    // Got an empty file_stat structure

    VRFY ( fd > 0, "bad arguments to file_addfile, fd <= 0" );
    VRFY ( fileno  > 0, "bad arguments to file_addfile, fileno <= 0" );

    fs.state = FS_INIT;
    fs.fd = fd;
    fs.file_no = fileno;
    fs.bytes = file_sz;
    fs.position = i;
    fs.poison = 0xFEEDC0DE;

    break;
  }

  memcpy( &file_stat[i], &fs, sizeof(struct file_stat_type) );
  return( &file_stat[i] );
}

struct file_stat_type* file_wait( uint64_t fileno, struct file_stat_type* test_fs ) {

  int i, k;

  while (1) {
    k = FILE_STAT_COUNT;

    for (i=0; i<=k; i++) {
      memcpy( test_fs, &file_stat[i], sizeof(struct file_stat_type) );

      if (test_fs->file_no != fileno)
        continue;

      if ( test_fs->poison != 0xFEEDC0DE )
        continue;

      if ( (test_fs->state == FS_INIT) && (__sync_val_compare_and_swap(
        &file_stat[i].state, FS_INIT, FS_COMPLETE|FS_IO) == FS_INIT ) )
      {
        DBV("NEW writer on fn=%ld", test_fs->file_no);
        return &file_stat[i];
      }

      if (test_fs->state & FS_IO) {
        DBV("ADD writer to fn=%ld", test_fs->file_no);
        return &file_stat[i];
      }
    }

    usleep(1000);
  }
}


struct file_stat_type* file_next( int id, struct file_stat_type* test_fs ) {

  // Generic function to fetch the next file, may return FD to multiple
  // threads depending on work load / incomming file stream.
  //
  DBV("[%2d] Enter file_next", id);

  int i, j, k, did_iteration=0, do_exit=0, active_file;


  while (1) {
    k = FILE_STAT_COUNT;
    active_file=0;

    for (i=1; i<=k; i++) {
      j = (FILE_STAT_COUNT + i) % FILE_STAT_COUNT;
      memcpy( test_fs, &file_stat[j], sizeof(struct file_stat_type) );

      if (test_fs->file_no == ~0UL) {
        do_exit=1;
        continue;
      }

      if (test_fs->fd == 0)
        continue;

      active_file=1;
      if ( (test_fs->state == FS_INIT) && (__sync_val_compare_and_swap(
        &file_stat[j].state, FS_INIT, FS_COMPLETE|FS_IO|(1<<id) ) == FS_INIT ) )
      {
        VRFY ( test_fs->poison == 0xFEEDC0DE, "Bad poison" );
        DBV("[%2d] NEW IOW on fn=%ld, slot=%d", id, test_fs->file_no, j);
        return &file_stat[j]; // Fist worker on file
      }

      uint64_t st = test_fs->state;
      if ( (test_fs->state & FS_IO) && (__sync_val_compare_and_swap(
        &file_stat[j].state, st, st| (1<<id) ) == st) )
      {
        VRFY ( test_fs->poison == 0xFEEDC0DE, "Bad poison" );
        DBV("[%2d] ADD IOW on fn=%ld", id, test_fs->file_no);
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



