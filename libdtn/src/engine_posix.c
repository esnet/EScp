#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include "args.h"
#include <zstd.h>


#include "file_io.h"

void* file_posixfetch( void* arg ) {
  struct file_object* fob = arg;
  // struct posix_op* op = fob->pvdr;

  VRFY( fob, "Fob undefined");
  if ((fob->head - fob->tail) >= 1)
    return 0;

  fob->head++;
  // op->fd = fob->fd;

  return fob->pvdr;
}

void* file_posixset( void* arg, int32_t key, uint64_t value ) {
  struct posix_op* op = (struct posix_op*) arg;

  switch ( key ) {
    case FOB_SZ:
      op->sz = value;
      break;
    case FOB_OFFSET:
      op->offset = value;
      break;
    case FOB_BUF:
      op->buf = (uint8_t*) value;
      break;
    case FOB_FD:
      op->fd  = (int32_t) value;
      break;
    case FOB_TRUNCATE:
      break;
    default:
      VRFY( 0, "Bad key passed to posixset" );
  }

  return 0;
}

void* file_posixget( void* arg, int32_t key ) {
  struct posix_op* op = (struct posix_op*) arg;

  switch ( key ) {
    case FOB_SZ:
      return (void*) op->sz;
    case FOB_OFFSET:
      return (void*) op->offset;
    case FOB_BUF:
      if (op->compressed)
        return (void*) (op->buf + op->compress_offset);
      else
        return (void*) op->buf;
    case FOB_FD:
      return (void*) ((uint64_t) op->fd);
    case FOB_COMPRESSED:
      return (void*) ((uint64_t) op->compressed);
    case FOB_HASH:
      return (void*) ((uint64_t) op->hash);
    default:
      VRFY( 0, "Bad key passed to posixset" );
  }
}


void file_posixflush( void* arg ) {
  (void) arg;
  return ;
}

void* file_posixsubmit( void* arg, int32_t* sz, uint64_t* offset ) {
  struct file_object* fob = arg;
  struct posix_op* op = (struct posix_op*) fob->pvdr;
  static __thread struct ZSTD_CCtx_s* zctx = NULL;
  uint64_t csz=0;
  bool do_write=true;

  if ( fob->tail < fob->head ) {

    if ( fob->io_flags & O_WRONLY ) {
      if (fob->sparse) {
        uint64_t i=0;
        for (i=0;i < op->sz; i++) {
          if ( ((uint8_t*)op->buf)[i] != 0 )
            break;
        }
        if (i == op->sz) {
          *sz = op->sz;
          do_write = false;
        }
      }
      if (do_write)
        *sz = pwrite( op->fd, op->buf, op->sz, op->offset );
    } else {
      *sz = pread( op->fd, op->buf, fob->blk_sz, op->offset );

      if (fob->do_hash) {
        op->hash = file_hash(op->buf, *sz, *offset/fob->blk_sz);
      }

      if (fob->compression) {

        if (!zctx) {
          zctx = ZSTD_createCCtx();
          VRFY(zctx > 0, "Couldn't allocate zctx");
        }

        op->compress_offset = fob->blk_sz;

        csz = ZSTD_compressCCtx( zctx,
          op->buf + fob->blk_sz, fob->blk_sz,
          op->buf, *sz, 3 );

        if ((csz > 0) && (csz < *sz)) {
          op->compressed=csz;
        } else {
          op->compressed=0;
          csz=0;
        }

      }
    }

    DBG( "[%2d] %s op fd=%d sz=%zd, offset=%zd %ld:%ld c=%zd",
      fob->id, fob->io_flags & O_WRONLY ? "write":"read",
      op->fd, fob->io_flags & O_WRONLY ? op->sz: fob->blk_sz,
      op->offset, fob->tail, fob->head, csz);

    offset[0] = op->offset;
    fob->tail++;

    if (sz[0] == -1) {
      sz[0] = -errno;
      VRFY(sz[0] < 0, "setting -errno failed");
    }
    return (void*) op;
  }

  return 0;
}

int file_posixtruncate( void* arg, int64_t sz ) {
  struct file_object* fob = arg;
  struct posix_op* op = (struct posix_op*) fob->pvdr;

  DBV( "[%2d] Truncate op fd=%d sz=%ld", fob->id, op->fd, sz );

  return ftruncate( op->fd, sz );
}

int file_posixclose(void* arg) {
  struct file_object* fob = arg;
  struct posix_op* op = (struct posix_op*) fob->pvdr;

  DBV( "[%2d] Close on fd=%d", fob->id, op->fd );
  return close( op->fd );
}

void* file_posixcomplete( void* arg, void* arg2 ) {
  return 0;
}

int file_posixinit( struct file_object* fob ) {
  struct posix_op *op;

  op = malloc( sizeof(struct posix_op) );
  VRFY( op, "bad malloc" );

  int flags = MAP_SHARED|MAP_ANONYMOUS;

  if (fob->hugepages)
    flags |= MAP_HUGETLB;

  uint64_t alloc_sz = fob->blk_sz;

  if (fob->compression)
    // We could do alloc_sz + FIO_COMPRESS_MARGIN if receiver. *=2 is OK
    // because fob->blk_sz always ?= 256K
    alloc_sz *= 2;


  op->buf = mmap( NULL, alloc_sz, PROT_READ|PROT_WRITE, flags, -1, 0 );
  VRFY( op->buf != (void*) -1, "mmap (block_sz=%d)", fob->blk_sz );

  fob->pvdr = op;
  fob->QD   = 1;

  fob->fetch    = file_posixfetch;
  fob->flush    = file_posixflush;
  fob->get      = file_posixget;
  fob->submit   = file_posixsubmit;
  fob->complete = file_posixcomplete;
  fob->set      = file_posixset;

  fob->open     = open;
  fob->close    = file_posixclose;

  fob->close_fd = close;
  fob->fopendir = fdopendir;
  fob->readdir  = readdir;

  fob->truncate = file_posixtruncate;
  fob->fstat    = fstat;

  return 0;
}


