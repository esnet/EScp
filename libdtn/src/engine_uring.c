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
#include "liburing.h"

#define URING_QD_MAX 32

struct uring_entry {
  struct posix_op pop;
  struct io_uring_sqe *sqe;
  struct io_uring_cqe *cqe;
};

struct uring_op {
  struct   uring_entry entry[URING_QD_MAX];
  struct   io_uring* ring;
  uint64_t map;
  int      order[URING_QD_MAX];
};

void* file_uringfetch( void* arg ) {
  struct file_object* fob = arg;
  struct posix_op* pop = fob->pvdr;
  struct uring_op* op = pop->ptr;
  struct iovec* iov = pop->ptr2;

  struct io_uring_sqe* sqe;

  if ( (fob->head - fob->tail) >= fob->QD ) {
    DBV("[%2d] file_uringfetch: fob->head (%zd) > fob->tail (%zd) + fob->QD (%d)",
       fob->id, fob->head, fob->tail, fob->QD );
    return 0;
  }

  sqe = io_uring_get_sqe( op->ring );
  if (! sqe ) {
    DBV("[%2d] file_uringfetch: !sqe", fob->id);
    return 0;
  }

  if (!sqe->user_data) {
    uint64_t i;
    for (i=1; i<=fob->QD; i++) {
      struct posix_op* p = (struct posix_op*) ((uint64_t)fob->pvdr + (i*sizeof( struct posix_op)) );
      if (!p->buf) {
        DBV("Assign SQE: %zd (%zX/%zX)", (uint64_t) i, (uint64_t) sqe, (uint64_t) iov[i-1].iov_base );
        p->buf = iov[i-1].iov_base;
        p->ptr2 = (void*) sqe;
        p->ptr = (void*) &iov[i-1];
        io_uring_sqe_set_data( sqe, (void*) i );
        break;
      }
    }
    VRFY( i<=fob->QD, "[%2d] file_uringfetch failed to acquire free IOV", fob->id );
  }

  {
    struct posix_op* p = (struct posix_op*) ((uint64_t)fob->pvdr +
                         ((uint64_t)sqe->user_data*sizeof( struct posix_op)) );

    p->ptr2 = (void*) sqe;
    DBV("[%2d] Using SQE: %zd/%d %zX\n", fob->id, (uint64_t) sqe->user_data, fob->QD, (uint64_t) p->buf );
    fob->head++;
    return p;
  }
}

void file_uringflush( void* arg, void* token ) {

  struct file_object* fob = arg;
  struct posix_op* pop = token;
  struct io_uring_sqe* sqe = (void*) pop->ptr2;
  struct uring_op* op = ((struct posix_op*) fob->pvdr)->ptr;

  ((struct iovec*) (pop->ptr))->iov_len = pop->sz;

  if ( fob->io_flags  & O_WRONLY ) {
    io_uring_prep_writev( sqe,
                          pop->fd,
                          pop->ptr, 1,
                          pop->offset
                        );
  } else {
    io_uring_prep_readv(  sqe,
                          pop->fd,
                          pop->ptr, 1,
                          pop->offset
                       );
  }

  DBV("[%2d] file_uringflush: %zd/%zX (%zX)\n",
      fob->id, (uint64_t) sqe->user_data, (uint64_t) pop->ptr, (uint64_t) sqe);

  VRFY ( io_uring_submit(op->ring) >= 0, "[%2d] io_uring_submit", fob->id );

}

void* file_uringsubmit( void* arg, int32_t* sz, uint64_t* offset ) {
  struct file_object* fob = arg;
  struct posix_op* pop = fob->pvdr;
  struct uring_op* op = pop->ptr;
  struct io_uring_cqe cqe_copy;

  if ( fob->head > fob->tail ) {
    struct io_uring_cqe *cqe;

    // match sqe to cqe, then return
    VRFY( io_uring_wait_cqe(op->ring, &cqe) == 0, "[%2d] io_uring_wait_cqe", fob->id );
    memcpy( &cqe_copy, cqe, sizeof( cqe_copy ) );

    uint64_t user_data = io_uring_cqe_get_data64( cqe );
    struct posix_op* p = (struct posix_op*) ((uint64_t)fob->pvdr +
                         (user_data*sizeof(struct posix_op)) );

    sz[0] = cqe->res;
    offset[0] = p->offset;
    p->ptr2 = cqe;

    if (fob->do_hash) {
      p->hash = file_hash( p->buf, *sz, *offset/fob->blk_sz );
    }

    return p;
  }

  return 0;

}

void* file_uringcomplete( void* arg, void* tok ) {
  struct posix_op* p = tok;
  struct file_object* fob = arg;
  struct posix_op* pop = fob->pvdr;
  struct uring_op* op= pop->ptr;

  fob->tail++;
  io_uring_cqe_seen( op->ring, p->ptr2 );

  return 0;
}

int file_uringinit( struct file_object* fob ) {
  int res, i;
  static __thread struct io_uring ring;
  static __thread struct uring_op op;


  // NOTE: We allocate one posix_op structure as a "root", then one for each
  //       entry in QD. These subsequent structures track the IO operation.
  struct posix_op *pop = malloc( sizeof(struct posix_op) * (fob->QD+1) );

  VRFY( pop, "bad malloc" );
  memset( &op, 0, sizeof(op) );
  memset( pop, 0, sizeof(struct posix_op)*(fob->QD+1) );

  {
    struct io_uring_params params = {0};

    /* 
    TODO: Add feature flags to engines. Ex: 

    params.flags = IORING_SETUP_SQPOLL; // Enable Submission Queue Polling
    params.sq_thread_idle = 10;         // Idle timeout in ms
    */

    res = io_uring_queue_init_params(fob->QD, &ring, &params);
    VRFY( res == 0, "Failed to initialize io_uring: %s", strerror(-res));
  }


  op.ring = &ring;

  struct iovec* iov;
  iov = malloc ( sizeof(struct iovec) * fob->QD );
  VRFY(iov, "bad malloc");

  for ( i=0; i<fob->QD; i++ ) {
    iov[i].iov_base = mmap (  NULL, fob->blk_sz,
      PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0 );
    VRFY(iov->iov_base, "mmap fail, QD=%d", i);
    DBV("[%2d] Allocated: %zX to %d\n", fob->id, (uint64_t) iov->iov_base, i+1);
    iov[i].iov_len = fob->blk_sz;
  }

  io_uring_register_buffers( &ring, iov, fob->QD );

  pop->ptr = &op;
  pop->ptr2 = iov;

  fob->pvdr = pop;

  fob->fetch    = file_uringfetch;
  fob->flush    = file_uringflush;
  fob->get      = file_posixget;
  fob->submit   = file_uringsubmit;
  fob->complete = file_uringcomplete;
  fob->set      = file_posixset;

  fob->open     = open;
  fob->close    = file_posixclose;

  fob->close_fd = close;
  fob->opendir  = opendir;
  fob->closedir = closedir;
  fob->readdir  = readdir;

  fob->truncate = file_posixtruncate;
  fob->fstat    = fstat;
  fob->preserve = file_posixpreserve;

  return 0;
}




