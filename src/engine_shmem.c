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

int file_shmemopen( const char* filename, int flags, ... ) {
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

int file_shmemstat( int fd, struct stat *statbuf ) {
  // Stat isn't typically used, so for now we return -1
  XLOG("file_shmemstat");
  return -1;
}

int file_shmemclose( int fd ) {
  VRFY( esshm, "esshm must be initialized before operations" );
  XLOG("lock: file_shmemclose");
  VRFY( pthread_mutex_lock( &esshm->file_lock ) == 0, );

  DBG("file_shmemclose: %d", fd);

  esshm->file_completed+=1;
  pthread_mutex_unlock( &esshm->file_lock );
  XLOG("release: file_shmemclose");

  return 0;
}

void* file_shmemfetch( void* arg ) {
  struct file_object* fob = arg;
  /*
  struct uring_op* op = fob->pvdr;
  */
  struct esshm_block* block=NULL;
  int i=0;

  XLOG("enter: file_shmemfetch");

  if ( esshm_QD >= esshm->QD ) {
    XLOG("current QD=%d > max QD=%d", esshm_QD, esshm->QD);
    return 0;
  }

  XLOG("lock: file_shmemfetch");
  VRFY( pthread_mutex_lock( &esshm->block_lock ) == 0, );

  for ( i=0; i< (esshm->QD * esshm->thread_count); i++ ) {
    if ( !esshm->block[i].state ) {
      block = &esshm->block[i];
      break;
    }
  }

  VRFY( !block->state, "Assert: Should never be true" ); 

  memset( block, 0, sizeof(struct esshm_block) );

  VRFY(block, "Assert: esshm block != NULL" );

  block->state=1;
  block->tid = fob->id;
  esshm_QD++;

  VRFY( pthread_mutex_unlock( &esshm->block_lock ) == 0, );
  XLOG("release: file_shmemfetch");
  return block;
}

void* file_shmemsubmit( void* arg, int32_t* sz, uint64_t* offset ) {

  /*
   *  0) Only look at blocks created by our thread
   *  1) convert any "1" states to "2"
   *  2) If there are any "4" states, return it work is already done
   *  4) Otherwise return 0.
   */

  struct file_object* fob = arg;
  int i, bn=0;
  int block_count = esshm->QD * esshm->thread_count;
  struct esshm_block *block, *ret=NULL;
  bool broadcast = false;

  if ( esshm_QD <= 0 )
    return 0;

  XLOG("lock: file_shmemsubmit");
  VRFY( pthread_mutex_lock( &esshm->block_lock ) == 0, );

  while (1) {
    // Loop until a block is ready for us

    for (i=0; i<block_count; i++) {
      block = &esshm->block[i];
      if ( block->tid == fob->id ) {
        switch ( block->state ) {
          case 1:
            XLOG("file_shmemsubmit [%d]: block %zX at offset %zX marked ready", 
                 fob->id, (uint64_t) block, block->op.offset );
            block->state=2;
            block->xsum=0xDecafC0ffeeULL;
            broadcast=true;
            break;
          case 4:
            bn=i;
            ret = block;
            break;
        }
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

  *sz     = ret->op.sz;
  *offset = ret->op.offset;
  {
    uint64_t param_sz = (sizeof(struct esshm_params) + 4095) & ~4095 ;
    ret->op.buf = (uint8_t*)(((uint64_t) esshm)+param_sz+(bn*esshm->block_sz));
  }
  XLOG("SHMEM submit;  found complete block  sz=%X os=%zX", *sz, *offset);
  return ret;
}

void* file_shmemcomplete( void* arg, void* tok ) {
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

void* file_shmemcleanup( void* arg ) {

  XLOG("lock: file_shmemcleanup");
  VRFY( pthread_mutex_lock( &esshm->block_lock ) == 0, );

  esshm->session_complete=1;
  pthread_cond_broadcast( &esshm->fetch_cv );

  VRFY( shm_unlink( esshm->shm_id ) == 0, );
  VRFY( pthread_mutex_unlock( &esshm->block_lock ) == 0, );

  XLOG("release: file_shmemcleanup");
  return 0;
}

int file_shmeminit( struct file_object* fob ) {
  static void* shm_region=0;
  int len, i, res;
  uint64_t shm_sz, param_sz;
  DBG( "[%2d] SHMEM init", fob->id )

  VRFY( managed, "shmem engine requires managed==True" );
  VRFY( pthread_mutex_lock( &file_next_lock ) == 0, );

  if (fob->id == 0) {
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
      ep->offset       = param_sz;
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

    MMSG("SHM\n%s", rand_path+9);

    VRFY( pthread_cond_broadcast( &file_next_cv ) == 0, );
  } else {
    while (!shm_region) {
      DBG( "[%2d] Wait for SHM Init", fob->id )
      pthread_cond_wait( &file_next_cv, &file_next_lock );
    }
  }

  DBG( "[%2d] Finish SHM Init", fob->id )


  VRFY( pthread_mutex_unlock( &file_next_lock ) == 0, );

  fob->pvdr     = shm_region;
  fob->submit   = file_shmemsubmit;
  fob->flush    = file_posixflush;
  fob->fetch    = file_shmemfetch;
  fob->complete = file_shmemcomplete;
  fob->get      = file_posixget;
  fob->set      = file_posixset;

  fob->open     = file_shmemopen;
  fob->close    = file_shmemclose;
  fob->fstat    = file_shmemstat;

  fob->cleanup  = file_shmemcleanup;
  return 0;
}


