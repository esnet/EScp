// Tool for testing enqueue/dequeue times

#include <stdio.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <pthread.h>
#include <sched.h>
#include <ck_barrier.h>
#include <ck_ring.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <assert.h>
#include <math.h>


#define CB_SIZE 16
#define MSG_TOT 1048576

typedef 
struct ring_element_s {
  long long in_time;
  char data[55];
} ring_element_t;


ck_ring_t ring;
ck_ring_buffer_t *buffer;
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
  ring_element_t tmp;

  int i = 0;
  for (i = 0; i < MSG_TOT; i++) {
    tmp.in_time = rdtsc();
    while ( false == ck_ring_enqueue_spsc_size(&ring, buffer, &tmp, &s)) {
      ck_pr_stall();
      //cpu_pause();
      in_stall++;
    }
  }
}


void dnq(void) {

  int i =0;
  
  ring_element_t tmp;
  while (1) {
    while (ck_ring_dequeue_spsc(&ring, buffer, &tmp) == true){
      if (++i>=MSG_TOT)
        return;
    }
    //cpu_pause();
    ck_pr_stall();
    out_stall++;
  }
}


int main(void){

  buffer = aligned_alloc(64, 64 * (CB_SIZE));
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
    double khz; fscanf(f, "%lf", &khz); fclose(f);
    double tsc_hz = khz * 1000.0;
    ns= ((end-start)* 1e9) / tsc_hz;
  }

  printf("OK! QD=%d #=%d Stalls in=%zd/out=%zd rdtsc Δ=%zd (ns=%0.9f)\n", CB_SIZE, MSG_TOT, in_stall, out_stall, end-start, ns);
  return 0;
}
