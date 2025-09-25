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

void* file_shmemsubmit( void* arg, int32_t* sz, uint64_t* offset ) {
  struct file_object* fob = arg;
  struct posix_op* op = (struct posix_op*) fob->pvdr;
  static __thread struct ZSTD_CCtx_s* zctx = NULL;
  uint64_t csz=0;
  bool do_write=true;

  if ( fob->tail < fob->head ) {

    if ( fob->io_flags & O_WRONLY ) {
      if (fob->sparse) {
        int res = memcmp_zero( op->buf, op->sz );
        if (res == 0) {
          *sz = op->sz;
          do_write = false;
        }
      }
      if (do_write)
        *sz = pwrite( op->fd, op->buf, op->sz, op->offset );
    } else {
      *sz = pread( op->fd, op->buf, fob->blk_sz, op->offset );
      if (*sz > 0) {

        if (fob->do_hash)
          op->hash = file_hash(op->buf, *sz, *offset/fob->blk_sz);

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
      } else {
        DBG("[%2d] Op failed (typ: read past EOF) %d:%s fd=%d %d/%d os=%zX",
          fob->id, errno, strerror(errno), op->fd, *sz, fob->blk_sz,
          op->offset );
      }
    }

    DBG( "[%2d] %s op fd=%d sz=%zd, offset=0x%zX %ld:%ld %s=%d",
      fob->id, fob->io_flags & O_WRONLY ? "write":"read",
      op->fd, fob->io_flags & O_WRONLY ? op->sz: fob->blk_sz,
      op->offset, fob->tail, fob->head,
      fob->io_flags & O_WRONLY ? "do_write":"compress",
      fob->io_flags & O_WRONLY ? do_write:(int) csz );

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

int file_shmemclose(void* arg) {
  return 0;
}

int file_shmemopen( const char* filename, int flags, ... ) {
  return 9999;
}

int file_shmeminit( struct file_object* fob ) {
  struct posix_op *op;

  op = malloc( sizeof(struct posix_op) );
  VRFY( op, "bad malloc" );
  memset( op, 0, sizeof(struct posix_op) );


  uint64_t alloc_sz = 1024 + (fob->blk_sz*fob->QD);
  VRFY( fob->compression == 0, "Compression not supported by SHM engine" );
  char buf[512];
  sprintf(buf, "/tmp.escp.test.%02d", fob->id);
  int fd = shm_open(buf, O_CREAT|O_RDWR, 0666);

  NFO("file_shmeminit: Initialized with '%s'", buf);
  VRFY(fd != -1, "Error, shm_open in file_shmeminint");

  op->buf = mmap( NULL, alloc_sz, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0 );
  VRFY( op->buf != MAP_FAILED, "mmap (block_sz=%d)", fob->blk_sz );

  fob->pvdr = op;
  fob->QD   = 1;

  fob->fetch    = file_posixfetch;
  fob->flush    = file_posixflush;
  fob->get      = file_posixget;
  fob->set      = file_posixset;

  fob->submit   = file_shmemsubmit;
  fob->complete = file_posixcomplete;
  fob->open     = file_shmemopen;
  fob->close    = file_shmemclose;

  fob->close_fd = close;
  fob->opendir  = opendir;
  fob->closedir = closedir;
  fob->readdir  = readdir;

  fob->truncate = file_dummytruncate;
  fob->fstat    = fstat;
  fob->preserve = file_dummypreserve;

  return 0;
}


