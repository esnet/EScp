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
  int i=0;
  int complete = 0;
  int64_t fd=-1, block_count=0, block_completed;

  struct esshm_params* ep;
  struct esshm_block*  block[64];
  struct esshm_block*  b;

  DBG("Start ESSHM client .01");

  if ( !argv[1] ) {
    printf("Usage: %s [shm-path] <log_file>\n", argv[0]);
    return 0;
  }

  ep = esshm_init(argv[1]);

  if (argv[2])
    debug = creat( argv[2], 0644 );

  if (!ep) {
    return 0;
  }

  while (!complete) {
    do { 

      while ( (b = esshm_fetch_nowait(ep)) ) {
        if (b) {
          DBG("fetch nowait block offset=%zX @%zX", b->op.offset, (uint64_t) b);
          block[block_count++] = b;
        }
      }

      if (!block_count) {
        b = esshm_fetch(ep);
        block[block_count++]=b;
        DBG("fetch wait block offset=%zX", b->op.offset);
      }

      qsort( block, block_count, sizeof( char* ), cmp_block );
    } while ( block[0]->op.offset != last_offset );

    /*
    if (block_count) {
      fprintf(stderr, "Current inventory (%d): \n", block_count);

      for (i=0; i<block_count; i++) {
        if (i) 
          fprintf(stderr, ", ");
        else 
          fprintf(stderr, "  ");
        fprintf(stderr, "%zX", block[i]->op.offset >> 16);
      }
      fprintf(stderr, "\n");
    }
    */

    block_completed=0;
    for (i=0; i< block_count; i++) {

      b = block[i];
      if (b->op.offset == last_offset)
        last_offset = b->op.offset + ep->block_sz;
      else
        break;

      block_completed++;

      DBG("submit block offset=%zX", b->op.offset);
      b->op.offset = offset;
      offset += ep->block_sz;
      b->op.sz = ep->block_sz;

      if (b->op.fd != fd) {
        DBG("Got filename %s", esshm_getfilename( ep, b ) );
        fd = b->op.fd;
      }

      if (offset > (16ULL * 1024 * 1024 * 1024 ) ) {
        b->op.sz = 0;
        complete = 1;
      }

      esshm_complete(ep, b);
      DBG("Completed =%zX", b->op.offset);
    }

    block_count -= block_completed;
    DBG("block_count=%zd block_completed=%zd", block_count, block_completed);
    memmove( &block[0], &block[block_completed], block_count * sizeof(char*) );
  }
  DBG("SHM complete!\n");

}
