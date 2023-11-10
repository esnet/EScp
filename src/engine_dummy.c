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
#include "args.h"
#include "file_io.h"

pthread_mutex_t dummy_lock;

static int dummy_fd_head=0;
static struct stat dummy_sb[THREAD_COUNT] = {0};


int file_dummyopen( const char* filename, int flags, ... ) {
  int fd;
  struct stat sb;

  if (flags & O_WRONLY)
    return 1;

  if ( (stat(filename, &sb) != 0) )
    return -1;

  VRFY( pthread_mutex_lock(&dummy_lock) == 0, );

  fd = ++dummy_fd_head ;
  VRFY( fd < THREAD_COUNT, "Dummy engine file count exceeded fd>%d",
             THREAD_COUNT);

  memcpy( &dummy_sb[fd], &sb, sizeof(sb) );

  pthread_mutex_unlock(&dummy_lock);

  return fd;
}

int file_dummystat( int fd, struct stat *sbuf ) {
  memcpy ( sbuf, &dummy_sb[fd], sizeof (struct stat) );
  return 0;
}

int file_dummytruncate( void*, int64_t ) {
  return 0;
}

int file_dummyclose( void* ) {
  VRFY( pthread_mutex_lock(&dummy_lock) == 0, );
  --dummy_fd_head;
  pthread_mutex_unlock(&dummy_lock);


  return 0;
}

void* file_dummysubmit( void* arg, int32_t* sz, uint64_t* offset ) {

  struct file_object* fob = arg;
  struct posix_op* op = (struct posix_op*) fob->pvdr;

  int64_t file_sz;

  if ( fob->tail >= fob->head )
    return 0;

  fob->tail++;



  if (fob->io_flags & O_WRONLY) {

    DBG( "[%2d] %s op fd=%d sz=%zd, offset=%zd",
      fob->id, fob->io_flags & O_WRONLY ? "write":"read",
      op->fd, fob->io_flags & O_WRONLY ? op->sz: fob->blk_sz,
      op->offset );

    // If receiver always return requested write size
    sz[0] = op->sz;
    return (void*) op;
  }

  VRFY( pthread_mutex_lock(&dummy_lock)==0, );
  file_sz = dummy_sb[op->fd].st_size;
  pthread_mutex_unlock(&dummy_lock);


  sz[0] = file_sz - op->offset;

  if (sz[0] > fob->blk_sz)
    sz[0] = fob->blk_sz;

  if (op->offset > file_sz)
    sz[0] = 0;

  DBG( "[%2d] %s op fd=%d sz=%zd, offset=%zd, file_total=%zd", 
    fob->id, fob->io_flags & O_WRONLY ? "write":"read",
    op->fd, fob->io_flags & O_WRONLY ? op->sz: fob->blk_sz,
    op->offset, file_sz );

  return (void*) op;
}

void* file_dummycomplete( void* arg, void* arg2 ) {
  return 0;
}


int file_dummyinit( struct file_object* fob ) {
  struct posix_op *op;

  pthread_mutex_init( &dummy_lock, NULL );

  op = malloc( sizeof(struct posix_op) );
  VRFY( op, "bad malloc" );

  op->buf = mmap (  NULL, fob->blk_sz, PROT_READ|PROT_WRITE,
                         MAP_SHARED|MAP_ANONYMOUS, -1, 0 );
  VRFY( op->buf != (void*) -1, "mmap (block_sz=%d)", fob->blk_sz );

  fob->pvdr = op;
  fob->QD   = 1;

  fob->submit   = file_dummysubmit;
  fob->flush    = file_posixflush;
  fob->fetch    = file_posixfetch;
  fob->complete = file_dummycomplete;
  fob->get      = file_posixget;
  fob->set      = file_posixset;

  fob->open     = file_dummyopen;
  fob->close    = file_dummyclose;
  fob->fstat    = file_dummystat;
  fob->truncate = file_dummytruncate;

  return 0;
}


