// Tool for testing enqueue/dequeue times

#include <stdio.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <pthread.h>
#include <sched.h>
#include <ck_barrier.h>
#include <ck_ring.h>
#include <ck_pr.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>
#include <assert.h>
#include <math.h>


/*
typedef struct {
  long long in_time;
  char data[45];
} ring_ele_t __attribute__((aligned(64))) ;
*/

struct ring_ele_t {
  long long in_time;
  char data[45];
} __attribute__((aligned(64))) ;

CK_RING_PROTOTYPE( ckt, ring_ele_t ) ;

#define CB_SIZE 16
#define MSG_TOT 1048576

ck_ring_t ring;
struct ring_ele_t *buffer;
pthread_t           tid1, tid2;

uint64_t in_stall=0, out_stall=0;

static inline uint64_t rdtsc(void) {
    unsigned int lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static inline void cpu_pause(void) {
    __asm__ __volatile__("pause");
}

void enq(void) {

  unsigned int s=64;
  struct ring_ele_t tmp;

  int i = 0;
  for (i = 0; i < MSG_TOT; i++) {
    tmp.in_time = rdtsc();
    while ( false == ck_ring_enqueue_spsc_ckt(&ring, buffer, &tmp) ) {
      ck_pr_stall();
      //cpu_pause();
      in_stall++;
    }
  }
}


void dnq(void) {

  int i =0;
  
  struct ring_ele_t tmp;
  while (1) {
    while (ck_ring_dequeue_spsc_ckt(&ring, buffer, &tmp) == true){
      if (++i>=MSG_TOT)
        return;
    }
    //cpu_pause();
    ck_pr_stall();
    out_stall++;
  }
}


int main(void){

  buffer = aligned_alloc(64, sizeof(struct ring_ele_t) * (CB_SIZE));
  assert(buffer!=NULL);

  ck_ring_init(&ring, CB_SIZE);

  uint64_t start = rdtsc();
  pthread_create (&tid1, NULL, (void *) (&enq), NULL);
  pthread_create (&tid2, NULL, (void *) (&dnq), NULL);

  pthread_join (tid1, NULL);
  pthread_join (tid2, NULL);
  uint64_t end = rdtsc();
  double ns=NAN;

  FILE *f = fopen("/sys/devices/system/cpu/cpu0/tsc_freq_khz", "r");
  if (f) {
    double khz; 
    if (fscanf(f, "%lf", &khz) != 1) {
      khz = 0;
    }
    fclose(f);
    double tsc_hz = khz * 1000.0;
    ns= ((end-start)* 1e9) / tsc_hz;
  }

  printf("OK! QD=%d #=%d Stalls in=%" PRIu64 "/out=%" PRIu64 " rdtsc Δ=%" PRIu64 " (ns=%0.9f)\n", CB_SIZE, MSG_TOT, in_stall, out_stall, end-start, ns);
  return 0;
}
