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
#include <ck_stack.h>
#include "args.h"
#include "file_io.h"

#define MAX_FDS 1200

typedef struct item_t {
    uint64_t size;
    struct stat sb;
    int fd;
    ck_stack_entry_t stack_entry;
} item_t __attribute__ ((aligned(64)));

static ck_stack_t stack = CK_STACK_INITIALIZER;
CK_STACK_CONTAINER(item_t, stack_entry, item_container)

static item_t* items __attribute__ ((aligned(64))) = 0;

int file_dummyopen( const char* filename, int flags, ... ) {
  // We stat the file on open, because we need to emulate a file of size foo and
  // without stating the file we don't actually know how big the file is.
  struct stat sb;

  if (flags & O_WRONLY)
    return 1;

  if ( (stat(filename, &sb) != 0) ) {
    DBG("file_dummyopen: stat error on %.60s, %s", filename, strerror(errno));
    return -1;
  }

  ck_stack_entry_t *entry = ck_stack_pop_mpmc(&stack);
  if (!entry) {
    DBG("file_dummyopen: n0 free FDs");
    errno = EMFILE;
    return -1;  // No free FDs
  }

  item_t *item = item_container(entry);
  memcpy( &item->sb, &sb, sizeof(sb) );
  memcpy( &item->size, &sb.st_size, sizeof(uint64_t) );

  DBG("file_dummyopen: %.60s->%d", filename, item->fd);

  return item->fd;
}

int file_dummystat( int fd, struct stat *sbuf ) {
  DBG("file_dummystat");
  memcpy ( sbuf, &items[fd].sb, sizeof (struct stat) );
  return 0;
}

int file_dummytruncate( int fd, int64_t size) {
  DBG("file_dummytruncate");
  return 0;
}

int file_dummyclose( void* arg ) {
  struct file_object* fob = arg;
  struct posix_op* op = (struct posix_op*) fob->pvdr;

  DBG( "[%2d] file_dummyclose on fd=%d", fob->id, op->fd );
  ck_stack_push_mpmc(&stack, &items[op->fd].stack_entry);

  return 0;
}

int file_dummyclosefd( int arg ) {

  DBG( "[%2d] file_dummyclose on fd=%d", -1, arg );

  ck_stack_push_mpmc(&stack, &items[arg].stack_entry);



  return 0;
}

void* file_dummysubmit( void* arg, int32_t* sz, uint64_t* offset ) {

  DBG("file_dummysubmit");
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

  file_sz = items[op->fd].size;
  sz_ret = file_sz - op->offset;

  if (sz_ret > fob->blk_sz)
    sz_ret = fob->blk_sz;

  if (file_sz <= op->offset)
    sz_ret = 0;
  else {
      if (fob->do_hash && !(fob->io_flags & O_WRONLY) && sz_ret) {
        uint8_t hash[32];
        file_hash(op->buf, sz_ret, hash);
        op->hash = ((uint32_t*)hash)[0];
      }
  }

  sz[0] = sz_ret;

  DBG( "[%2d] read op fd=%d sz=%d, offset=%zd, file_total=%zd",
    fob->id,
    op->fd, sz[0],
    op->offset, file_sz );

  return (void*) op;
}

void* file_dummypreserve(int32_t fd, uint32_t mode, uint32_t uid, uint32_t gid, int64_t atim_sec, int64_t atim_nano, int64_t mtim_sec, int64_t mtim_nano) {
  return 0;
}

int file_dummyinit( struct file_object* fob ) {
  struct posix_op *op;


  op = malloc( sizeof(struct posix_op) );
  VRFY( op, "bad malloc (op)" );
  memset( op, 0, sizeof(struct posix_op) );

  struct item_t* i = ck_pr_load_ptr(&items);

  if (i == 0) {
    if (ck_pr_cas_64((uint64_t*) &items, 0, 1)) {
      i = aligned_alloc( 64, sizeof( struct item_t ) * MAX_FDS );
      VRFY( i, "bad malloc (items)" );

      ck_stack_init(&stack);
      for (int j = MAX_FDS - 1; j >= 1; j--) {
        i[j].fd = j;
        ck_stack_push_mpmc(&stack, &i[j].stack_entry);
      }

      ck_pr_store_ptr( &items, i );
    }
  }


  op->buf = mmap (  NULL, fob->blk_sz, PROT_READ|PROT_WRITE,
                         MAP_SHARED|MAP_ANONYMOUS, -1, 0 );
  VRFY( op->buf != (void*) -1, "mmap (block_sz=%d)", fob->blk_sz );

  fob->pvdr = op;
  fob->QD   = 1;
  DBG("[%2d] file_dummyinit with buf=%016zX", fob->id, (uint64_t) op->buf);

  fob->submit   = file_dummysubmit;
  fob->flush    = file_posixflush;
  fob->fetch    = file_posixfetch;
  fob->complete = file_posixcomplete;
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

  while (1) {
    void* ptr = ck_pr_load_ptr(&items);
    uint64_t i = (uint64_t) ptr;

    if ( (i==0) || (i==1) ) {
      ESCP_DELAY(1);
    } else {
      break;
    }
  }

  return 0;
}


