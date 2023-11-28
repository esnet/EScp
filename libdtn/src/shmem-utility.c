#include <stdlib.h>

#include "es_shm.h"

/* 1) Add 2nd argument, personality, i.e
 *
 *     pipe:xsum,stat
 *     test:prng,rnds.10k.1g,xsum
 *
 */

static int cmp_block( const void *x, const void* y ) {
  const struct esshm_block **a, **b;

  a=(const struct esshm_block**) x;
  b=(const struct esshm_block**) y;

  if (a[0]->op.offset > b[0]->op.offset)
    return 1;
  return -1;
}

int debug=0;

#define DBG(x, ...) if (debug)  {                               \
  char bu[2200]; int len;                                       \
  len = snprintf( bu, 2100, "[DBG] " x "\n", ##__VA_ARGS__ );   \
  if (write( debug, bu, len )) {} ;                             \
}

int main(int argc, char** argv) {
  uint64_t offset=0, last_offset=0;
  int i=0, do_wait, block_count=0;
  int64_t fd=-1, block_completed;

  struct esshm_params* ep;
  struct esshm_block*  block[256];
  struct esshm_block*  b;

  if ( !argv[1] ) {
    printf("Usage: %s [shm-path] <log_file>\n", argv[0]);
    return 0;
  }

  ep = esshm_init(argv[1]);

  if (argv[2])
    debug = creat( argv[2], 0644 );

  DBG("Start ESSHM client .01");

  if (!ep) {
    return 0;
  }

  while (1) {
    do_wait=0;
    do {
      // Get a block and then sort it based on offset

      while ( (b = esshm_fetch_nowait(ep)) ) {
        if (b) {
          block[block_count++] = b;
          DBG("fetch nowait block offset=%zX bc=%d",
              b->op.offset, block_count);
        }
        do_wait=0;
      }

      if (do_wait) {
        DBG("Waiting for offset=%zX", last_offset);
        b = esshm_fetch(ep);
        block[block_count++]=b;
        DBG("fetch wait block offset=%zX bc=%d", b->op.offset, block_count);
      }
      do_wait=1;

      qsort( block, block_count, sizeof( void* ), cmp_block );
    } while ( block[0]->op.offset != last_offset ); // offset > expected

    block_completed=0;
    for (i=0; i< block_count; i++) {
      // Process blocks until expected offset != actual

      b = block[i];
      if (b->op.offset == last_offset)
        last_offset += ep->block_sz;
      else
        break;

      DBG("submit block offset=%zX", b->op.offset);

      b->op.offset = offset;
      offset += (uint64_t) ep->block_sz;
      b->op.sz = ep->block_sz;

      if (b->op.fd != fd) {
        DBG("Got filename %s", esshm_getfilename( ep, b ) );
        fd = b->op.fd;
      }

      if (offset > (16ULL * 1024 * 1024 * 1024 ) ) {
        b->op.sz = 0;
      }

      {
        uint8_t* buf = b->op.buf + ( (uint64_t) ep );
        DBG("-> %08X \n", file_hash(buf, b->op.sz, 0) );
      }

      esshm_complete(ep, b);
      block_completed++;

      DBG("Completed =%zX", b->op.offset);
    }

    block_count -= block_completed;
    DBG("block_count=%d block_completed=%zd", block_count, block_completed);
    memmove( &block[0], &block[block_completed], block_count * sizeof(void*) );
  }
  DBG("SHM complete!\n");

}
