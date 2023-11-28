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
  struct uring_op* op = fob->pvdr;
  int i;

  if ( (fob->head - fob->tail) >= fob->QD )
    return 0;

  for (i=0; i<fob->QD; i++) {
    if ( !(op->map & (1 <<  i)) )
      break;
  }
  VRFY( i < fob->QD, "Internal Error" );

  op->entry[i].sqe = io_uring_get_sqe( op->ring );
  if (! op->entry[i].sqe )
    return 0;

  op->map |= 1 << i;
  op->order[fob->head%fob->QD] = i;
  fob->head++;

  op->entry[i].pop.fd = fob->fd;
  op->entry[i].pop.truncate = 0;
  return &op->entry[i];
}

void file_uringflush( void* arg ) {
  struct file_object* fob = arg;
  struct uring_op* op = fob->pvdr;

  static __thread uint64_t last_submit=0;
  bool did_work = false;
  uint64_t i, j;

  for (i=last_submit; i < fob->head; i++) {
    did_work = true;
    j = op->order[i % fob->QD];
    if ( fob->io_flags  & O_WRONLY ) {
      io_uring_prep_writev( op->entry[j].sqe,
                            op->entry[j].pop.fd,
                            &op->entry[j].pop.vec, 1,
                            op->entry[j].pop.offset
                          );
    } else {
      io_uring_prep_readv(  op->entry[j].sqe,
                            op->entry[j].pop.fd,
                            &op->entry[j].pop.vec, 1,
                            op->entry[j].pop.offset
                         );
    }
    op->entry[j].sqe->user_data = op->entry[j].pop.offset;
  }

  if (did_work) {
    last_submit = fob->head;
    VRFY ( io_uring_submit(op->ring) >= 0, "io_uring_submit" );
  }

}

void* file_uringsubmit( void* arg, int32_t* sz, uint64_t* offset ) {
  struct file_object* fob = arg;
  struct uring_op* op = fob->pvdr;

  file_uringflush(fob);

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
  struct uring_op* op= fob->pvdr;
  struct posix_op* pop = &entry->pop;

  if ( pop->truncate ) {
    DBG("[%2d] Applying truncate to fd=%d len=%zd",
      fob->id, pop->fd, pop->truncate);
    VRFY( ftruncate( pop->fd, pop->truncate ) == 0, );
  }

  fob->tail++;

  io_uring_cqe_seen( op->ring, entry->cqe );
  return 0;
}

int file_uringinit( struct file_object* fob ) {
  int res, i;
  static __thread struct io_uring ring;
  static __thread struct uring_op op;

  memset( &op, 0, sizeof(op) );

  res = io_uring_queue_init(fob->QD, &ring, 0);
  if (res < 0) {
    DBG("Failed to initialize io_uring: %s", strerror(-res));
    return res;
  }

  op.ring = &ring;

  for ( i=0; i<fob->QD; i++ ) {
    op.entry[i].pop.buf = mmap (  NULL, fob->blk_sz,
      PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0 );
    VRFY(op.entry[i].pop.buf, "mmap fail, QD=%d", i);
  }

  fob->pvdr = &op;

  fob->fetch    = file_uringfetch;
  fob->get      = file_posixget;
  fob->set      = file_posixset;

  fob->flush    = file_uringflush;
  fob->submit   = file_uringsubmit;
  fob->complete = file_uringcomplete;

  fob->open     = open;
  fob->close    = close;
  fob->fstat    = fstat;

  return 0;
}




