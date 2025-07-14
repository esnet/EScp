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

  int i;

  if ( (fob->head - fob->tail) >= fob->QD )
    return 0;

  for (i=0; i<fob->QD; i++) {
    if ( !(op->map & (1 <<  i)) )
      break;
  }
  VRFY( i < fob->QD, "Internal Error" );

  sqe = io_uring_get_sqe( op->ring );
  if (! sqe )
    return 0;

  // op->map |= 1 << i;
  // op->order[fob->head%fob->QD] = i;
  //


  // XXX: We check if the sqe is already set. If it isn't, then we set it
  // with the next available buffer.

  if (fob->head < fob->QD) {
    VRFY( sqe->user_data == 0, "Assert failed, user data should be blank" );
    io_uring_sqe_set_data( sqe, &iov[fob->head] );
  }

  fob->head++;


  return sqe;
}

// mojibake: should take sqe as argument...
void file_uringflush( void* arg, void* token ) {

  struct io_uring_sqe* sqe = token;
  struct file_object* fob = arg;
  struct posix_op* pop = fob->pvdr;
  struct uring_op* op = pop->ptr;

  struct iovec* iov = (struct iovec*) sqe->user_data;

  if ( fob->io_flags  & O_WRONLY ) {
    io_uring_prep_writev( sqe,
                          pop->fd,
                          iov, 1,
                          pop->offset
                        );
  } else {
    io_uring_prep_readv(  sqe,
                          pop->fd,
                          iov, 1,
                          pop->offset
                       );
  }

  VRFY ( io_uring_submit(op->ring) >= 0, "io_uring_submit" );

}

void* file_uringsubmit( void* arg, int32_t* sz, uint64_t* offset ) {
  struct file_object* fob = arg;
  struct posix_op* pop = fob->pvdr;
  struct uring_op* op = pop->ptr;

  if ( fob->head > fob->tail ) {
    struct io_uring_cqe *cqe;
    int i;

    // match sqe to cqe, then return
    VRFY( io_uring_wait_cqe(op->ring, &cqe) >= 0, "io_uring_wait_cqe" );

    sz[0] = cqe->res;
    offset[0] = cqe->user_data;

    for ( i=0; i<fob->QD; i++ ) {
      if (op->entry[i].pop.offset == offset[0]) {
        op->entry[i].cqe = cqe;
        op->map &= ~(1 << i);
        return &op->entry[i];
      }
    }

    VRFY( 0, "Couldn't find CQE associated with SQE\n");
  }

  return 0;

}

void* file_uringcomplete( void* arg, void* tok ) {
  struct uring_entry* entry = tok;
  struct file_object* fob = arg;
  struct posix_op* pop = fob->pvdr;
  struct uring_op* op= pop->ptr;

  fob->tail++;
  io_uring_cqe_seen( op->ring, entry->cqe );

  return 0;
}

int file_uringinit( struct file_object* fob ) {
  int res, i;
  static __thread struct io_uring ring;
  static __thread struct uring_op op;

  memset( &op, 0, sizeof(op) );

  struct posix_op *pop;

  pop = malloc( sizeof(struct posix_op) );
  VRFY( pop, "bad malloc" );
  memset( pop, 0, sizeof(struct posix_op) );

  {
    struct io_uring_params params = {0};

    params.flags = IORING_SETUP_SQPOLL;
    params.sq_thread_idle = 10; // Idle timeout in ms

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




