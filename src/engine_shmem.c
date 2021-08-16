#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include "es_shm.h"
#include "args.h"

static struct esshm_params* esshm=0; // shared memory global struct
static __thread int esshm_QD=0;      // Keep track of per thread QD

int shmem_open( const char* filename, int flags, ... ) {
  VRFY( esshm, "esshm must be initialized before operations" );

  XLOG("lock: file_shmemopen");
  VRFY( pthread_mutex_lock( &esshm->file_lock ) == 0, );
  DBG("file_shmemopen: %s %x", filename, flags );

  strncpy( esshm->file_name[esshm->file_next%esshm->file_slots],
    filename, 256 );
  esshm->file_next+=1;
  esshm->file_flags = flags;

  pthread_mutex_unlock( &esshm->file_lock );
  XLOG("release: file_shmemopen");

  return esshm->file_next;
}

int shmem_stat( int fd, struct stat *statbuf ) {
  // Stat isn't typically used, so for now we return -1
  XLOG("file_shmemstat");
  return -1;
}

int shmem_close( int fd ) {
  VRFY( esshm, "esshm must be initialized before operations" );
  XLOG("lock: file_shmemclose");
  VRFY( pthread_mutex_lock( &esshm->file_lock ) == 0, );
  esshm->file_completed+=1;
  pthread_mutex_unlock( &esshm->file_lock );
  XLOG("release: file_shmemclose");

  return 0;
}

void* shmem_fetch( void* arg ) {
  struct file_object* fob = arg;
  /*
  struct uring_op* op = fob->pvdr;
  */
  struct esshm_block* block=NULL;
  int bn;


  if ( esshm_QD >= esshm->QD ) {
    XLOG("current QD=%d > max QD=%d", esshm_QD, esshm->QD);
    return 0;
  }

  bn = (esshm_QD * esshm->thread_count) + fob->id;
  block = &esshm->block[bn];
  memset( block, 0, sizeof(struct esshm_block) );

  {
    uint64_t param_sz = (sizeof(struct esshm_params) + 4095) & ~4095 ;
    block->op.buf = (uint8_t*)
      (((uint64_t) esshm)+param_sz+(bn*esshm->block_sz));
  }

  block->state=1;
  block->tid = fob->id;
  esshm_QD++;

  XLOG("[%2d] file_shmemfetch: returned bn=%d bs=%X buf=%zX",
    fob->id, bn, esshm->block_sz, (uint64_t) block->op.buf );

  return block;
}

void* shmem_submit( void* arg, int32_t* sz, uint64_t* offset ) {

  /*
   *  0) Only look at blocks created by our thread
   *  1) convert any "1" states to "2"
   *  2) If there are any "4" states, return it work is already done
   *  4) Otherwise return 0.
   */

  struct file_object* fob = arg;
  int i, bn=0;
  struct esshm_block *block, *ret=NULL;
  bool broadcast = false;
  static __thread int finished = 0;

  if ( esshm_QD <= 0 )
    return 0;

  if ( finished ) 
    return 0;

  XLOG("lock: file_shmemsubmit");
  VRFY( pthread_mutex_lock( &esshm->block_lock ) == 0, );


  while ( 1 ) {
    // Mark blocks ready for SHM consumer AND ingest consumer blocks

    for (i=0; i<esshm_QD; i++) {
      bn = (i * esshm->thread_count) + fob->id;
      block = &esshm->block[bn];
      switch ( block->state ) {
        case 1:
          // Mark block ready from consumer
          XLOG("engine_[%d]: block %zX at offset %zX marked ready",
               fob->id, (uint64_t) block, block->op.offset );
          block->state=2;
          broadcast=true;
          break;
        case 4:
          // Block from consumer
          bn=i;
          ret = block;
          break;
      }
    }

    if (broadcast)
      pthread_cond_broadcast( &esshm->fetch_cv );

    if (ret) {
      break;
    }

    XLOG("cond_wait: file_shemsubmit");
    pthread_cond_wait( &esshm->submit_cv, &esshm->block_lock );

    XLOG("cond_resume: file_shemsubmit");
  }

  XLOG("release: file_shemsubmit");
  VRFY( pthread_mutex_unlock( &esshm->block_lock ) == 0, );

  if ( !ret->op.sz ) {
    XLOG("[%2d] file_shemsubmit: finished is asserted", fob->id);
    finished=1;
  }

  *sz     = ret->op.sz;
  *offset = ret->op.offset;

  {
    uint64_t param_sz = (sizeof(struct esshm_params) + 4095) & ~4095 ;
    ret->op.buf = (uint8_t*)(((uint64_t) esshm)+param_sz+(bn*esshm->block_sz));
  }

  XLOG("SHMEM submit;  found complete block  sz=%X os=%zX", *sz, *offset);
  return ret;
}

void* shmem_complete( void* arg, void* tok ) {
  struct esshm_block *block;
  block = tok;
  XLOG("lock: file_shmemcomplete");
  VRFY( pthread_mutex_lock( &esshm->block_lock ) == 0, );
  esshm_QD--;
  block->state=0;
  VRFY( pthread_mutex_unlock( &esshm->block_lock ) == 0, );
  XLOG("release: file_shmemcomplete");

  return 0;
}

void* shmem_cleanup( void* arg ) {

  XLOG("lock: file_shmemcleanup");
  VRFY( pthread_mutex_lock( &esshm->block_lock ) == 0, );

  esshm->session_complete=1;
  pthread_cond_broadcast( &esshm->fetch_cv );

  VRFY( shm_unlink( esshm->shm_id ) == 0, );
  VRFY( pthread_mutex_unlock( &esshm->block_lock ) == 0, );

  XLOG("release: file_shmemcleanup");
  return 0;
}

int shmem_init( struct file_object* fob ) {
  static void* shm_region=0;

  int len, i, res;
  uint64_t shm_sz, param_sz;
  DBG( "[%2d] SHMEM init", fob->id )

  VRFY( managed, "shmem engine requires managed==True" );
  VRFY( pthread_mutex_lock( &file_next_lock ) == 0, );

  if (!shm_region) {
    uint8_t rand_path[64] = {0};

    file_randrd( rand_path, 9 );
    len = file_b64encode( rand_path+14, rand_path, 9 );
    for (i=0; i<len; i++) {
      if (rand_path[14+i] == '/')
        rand_path[14+i] = '_';
      if (rand_path[14+i] == '+')
        rand_path[14+i] = '-';
    }
    memcpy( rand_path+9, "/dtn-", 5 );

    res = shm_open( (char*) (rand_path+9), O_RDWR|O_CREAT|O_EXCL, 0600 );
    VRFY( res != -1, "shm_open error" );

    param_sz = (sizeof(struct esshm_params) + 4095) & ~4095 ;
    shm_sz = (fob->thread_count*fob->blk_sz*fob->QD) + param_sz;

    VRFY( ftruncate( res, shm_sz ) == 0, );

    shm_region = mmap(NULL, shm_sz, PROT_READ|PROT_WRITE, MAP_SHARED, res, 0);
    VRFY( shm_region != (void*) -1UL, );

    memset(shm_region, 0, shm_sz);

    VRFY( fob->QD >= 1, "Bad QD specified, should be >=1" );
    {
      struct esshm_params *ep;
      pthread_mutexattr_t attr;
      pthread_condattr_t  attrcond;


      ep = shm_region;
      ep->magic = 0xFeedBea7C0ffeeUL;
      ep->thread_count = fob->thread_count;
      ep->block_sz     = fob->blk_sz;
      ep->QD           = fob->QD;
      ep->file_slots   = SHM_FILE_SLOTS;
      ep->param_sz     = param_sz;
      ep->poison       = 0xDeadC0deDeadC0deUL;
      ep->version      = SHM_VERSION;
      memcpy( ep->shm_id, rand_path+9, strlen((const char*)(rand_path+9)) );
      esshm = ep;

      pthread_mutexattr_init(&attr);
      VRFY(pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) == 0,);

      pthread_mutex_init( &ep->block_lock, &attr );
      pthread_mutex_init( &ep->file_lock,  &attr );

      pthread_condattr_init(&attrcond);
      pthread_condattr_setpshared(&attrcond, PTHREAD_PROCESS_SHARED);

      pthread_cond_init( &ep->fetch_cv, &attrcond  );
      pthread_cond_init( &ep->submit_cv, &attrcond );

    }

    NFO("[%2d] SHM %s sz=%zd QD=%d tc=%d", fob->id, rand_path+9,  (fob->thread_count*fob->blk_sz*fob->QD) + param_sz, fob->QD, fob->thread_count )
    MMSG("SHM\n%s", rand_path+9);

    VRFY( pthread_cond_broadcast( &file_next_cv ) == 0, );
  }

  DBG( "[%2d] Finish SHM Init", fob->id )


  VRFY( pthread_mutex_unlock( &file_next_lock ) == 0, );

  fob->pvdr     = shm_region;
  fob->submit   = shmem_submit;
  fob->flush    = file_posixflush;
  fob->fetch    = shmem_fetch;
  fob->complete = shmem_complete;
  fob->get      = file_posixget;
  fob->set      = file_posixset;

  fob->open     = shmem_open;
  fob->close    = shmem_close;
  fob->fstat    = shmem_stat;

  fob->cleanup  = shmem_cleanup;
  return 0;
}


