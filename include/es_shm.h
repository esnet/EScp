#ifndef __ESSHM_INCLUDE__
#define __ESSHM_INCLUDE__

#include <pthread.h>
#include <stdint.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>


#include "file_io.h"

#define SHM_FILE_SLOTS 64
#define SHM_BLOCK_SLOTS 128
#define SHM_VERSION 0x0005

/* ESshm API Public/Client interface */

static pthread_mutex_t esshm_init_lock;

struct esshm_block {
  struct posix_op op;
  uint8_t  state;
  uint16_t tid;
} __attribute__ ((aligned(64)));

struct esshm_params {

  uint64_t magic;

  int thread_count;
  int block_sz;
  int QD;
  int file_slots;
  int param_sz;
  int debug_level;
  int file_flags;

  uint64_t file_completed;
  uint64_t file_total;
  uint64_t file_next;

  uint64_t file_size[SHM_FILE_SLOTS];
  char file_name[SHM_FILE_SLOTS][256];


  struct esshm_block block[SHM_BLOCK_SLOTS] __attribute__ ((aligned(64)));
  uint64_t block_next;

  uint32_t version;
  uint8_t  session_complete;
  char     shm_id[32];

  uint64_t poison;

  pthread_mutex_t file_lock;
  pthread_mutex_t block_lock;
  pthread_cond_t  fetch_cv;
  pthread_cond_t  submit_cv;

};

inline static char* esshm_getfilename(
  struct esshm_params* ep, struct esshm_block* block ) {

  /* Convenience function to fetch the filename associated with FD */

  return ep->file_name[(block->op.fd % ep->file_slots)];
}

inline static struct esshm_params* esshm_init( const char* filename ) {
  /*
   * static int esshm_init( const char* shm_region_from_dtn_init );
   *
   *   Finds shared memory region pointed to by filename
   *
   * Returns: ptr to esshm_params* struct or 0 on error
   */

  int res;
  void* ret;
  static void* shm_region = NULL;

  pthread_mutex_lock( &esshm_init_lock );

  if (shm_region) {
    pthread_mutex_unlock( &esshm_init_lock );
    return shm_region;
  }

  res = shm_open( filename, O_RDWR, 0 );
  if ( res == -1 ) {
    fprintf( stderr, "Error opening SHM_region: %s\n", strerror(errno) );
    return 0;
  }

  {
    struct stat sb;
    fstat( res, &sb );
    ret = mmap(NULL, sb.st_size, PROT_READ|PROT_WRITE,
                      MAP_SHARED, res, 0 );
    fprintf( stderr, "Opened SHM region of sz=%zd\n", sb.st_size);
  }


  if (ret == (void*) -1UL) {
    fprintf( stderr, "Error mmap'ing shm region: %s\n", strerror(errno) );
    return 0;
  }
  {
    struct esshm_params* ep = ret;
    if ( ep->magic != 0xFeedBea7C0ffeeUL ) {
      fprintf( stderr, "Bad magic on shared memory region" );
      return 0;
    }

    if ( ep->version != SHM_VERSION ) {
      fprintf( stderr, "SHM Version mismatch, exiting." );
      return 0;
    }
  }

  pthread_mutex_unlock( &esshm_init_lock );

  shm_region = ret;
  return ret;
};

inline static struct esshm_block* __esshm_fetch(struct esshm_params* ep) {
  int i, block_count = ep->QD * ep->thread_count;
  struct esshm_block *block;

   // Internal: Look for any block with state set to 2;
   //           - If found, set state to 3 and return
   //           - Otherwise wait on fetch_cv and re-do check

  for( i=0; i<block_count; i++ ) {
    block = &ep->block[i];
    if (block->state == 2) {
      block->state=3; 
      return block;
    }
  }

  return NULL;
}


inline static struct esshm_block* esshm_fetch_nowait(struct esshm_params* ep) {
  /*
   * esshm_block* esshm_fetch_nowait (esshm_params*);
   *   gets next available I/O block or 0 if not available
   *
   * Returns: esshm_block* or 0 if no block available
   */


   struct esshm_block *ret=NULL;

   pthread_mutex_lock( &ep->block_lock );
   ret = __esshm_fetch(ep);
   pthread_mutex_unlock( &ep->block_lock );

   return ret;

}


inline static struct esshm_block* esshm_fetch( struct esshm_params* ep ) {

  /*
   * esshm_block* esshm_fetch (esshm_params*); gets next available I/O block
   *
   * Returns: esshm_block* or 0 if no block available
   */


   struct esshm_block *ret=NULL;

   pthread_mutex_lock( &ep->block_lock );
   while ( !ep->session_complete ) {

     ret = __esshm_fetch(ep);
     if ( ret )
       break;

     pthread_cond_wait( &ep->fetch_cv, &ep->block_lock );
   }
   pthread_mutex_unlock( &ep->block_lock );

   return ret;
}

inline static int esshm_complete(
  struct esshm_params* ep, struct esshm_block* block ) {

  /*
   * esshm_block* esshm_complete(params, block);
   *   Marks block as complete
   *
   * Returns: 0 on success, otherwise error
   */


  pthread_mutex_lock( &ep->block_lock );

  if (block->state != 3) {
    fprintf(stderr, "Error, block in incorrect state\n");
    pthread_mutex_unlock( &ep->block_lock );
    return -1;
  }

  block->state=4;
  pthread_cond_broadcast( &ep->submit_cv );
  pthread_mutex_unlock( &ep->block_lock );

  return 0;
}

/* Private interface */

extern pthread_mutex_t file_next_lock;
extern pthread_cond_t file_next_cv;

int file_shmemopen( const char* filename, int flags, ... );
int file_shmemstat( int fd, struct stat *statbuf );
int file_shmemclose( int fd );
void* file_shmemfetch( void* arg );
void* file_shmemsubmit( void* arg, int32_t* sz, uint64_t* offset );
void* file_shmemcomplete( void* arg, void* tok );
int file_shmeminit( struct file_object* fob );





#endif
