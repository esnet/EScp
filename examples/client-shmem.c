#include "es_shm.h"

int main(int argc, char** argv) {
  uint64_t offset=0;
  int i=0;
  int complete = 0;

  struct esshm_params* ep; 
  struct esshm_block*  block; 

  ep = esshm_init(argv[1]);

  if (!ep) {
    return 0;
  }

  while (!complete) {
    block = esshm_fetch(ep) ;

    if (block) {
      i++;

      block->op.offset = offset;
      offset += ep->block_sz;
      block->op.sz = ep->block_sz; 
      // memset( block->op.buf, i, ep->block_sz );

      if (offset > (1ULL * 1024 * 1024 * 1024 * 1024 ) ) {
        block->op.sz = 0;
      }

      esshm_complete(ep, block);
    } else {
      complete = 1;
    }
  }

}
