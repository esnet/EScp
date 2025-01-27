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
#include <dirent.h>
#include <fcntl.h>
#include "args.h"
#include "file_io.h"

pthread_mutex_t dummy_lock;

static int dummy_fd_head=0;
static struct stat dummy_sb[THREAD_COUNT] __attribute__ ((aligned(64))) = {0};


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

int file_dummytruncate( int fd, int64_t size) {
  return 0;
}

int file_dummyclose( void* ptr) {
  VRFY( pthread_mutex_lock(&dummy_lock) == 0, );
  --dummy_fd_head;
  pthread_mutex_unlock(&dummy_lock);


  return 0;
}

void* file_dummysubmit( void* arg, int32_t* sz, uint64_t* offset ) {

  struct file_object* fob = arg;
  struct posix_op* op = (struct posix_op*) fob->pvdr;

  int64_t file_sz;
  int64_t sz_ret;

  if ( fob->tail >= fob->head )
    return 0;

  fob->tail++;

  VRFY( fob->compression==0, "Compression not supported by Dummy Engine");

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


  sz_ret = file_sz - op->offset;

  if (sz_ret > fob->blk_sz)
    sz_ret = fob->blk_sz;


  if (file_sz <= op->offset)
    sz_ret = 0;
  else {
      if (fob->do_hash && !(fob->io_flags & O_WRONLY) && sz_ret)
        op->hash = file_hash( op->buf, sz_ret, *offset/fob->blk_sz );
  }

  sz[0] = sz_ret;

  DBG( "[%2d] read op fd=%d sz=%d, offset=%zd, file_total=%zd", 
    fob->id, 
    op->fd, sz[0],
    op->offset, file_sz );

  return (void*) op;
}

void* file_dummycomplete( void* arg, void* arg2 ) {
  return 0;
}

int file_dummyclosefd( int size) {
  return 0;
}

void* file_dummypreserve(int32_t fd, uint32_t mode, uint32_t uid, uint32_t gid, int64_t atim_sec, int64_t atim_nano, int64_t mtim_sec, int64_t mtim_nano) {
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

  fob->close_fd = file_dummyclosefd;
  fob->opendir = opendir;
  fob->closedir = closedir;
  fob->readdir  = readdir;

  fob->preserve = file_dummypreserve;

  return 0;
}


