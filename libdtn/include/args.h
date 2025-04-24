#ifndef __ARGS_H__
#define __ARGS_H__

#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>

#include <stdio.h>
#include <sys/time.h>

#include <syslog.h>

// Delay for x time in milli-seconds.
#define ESCP_DELAY(x) {               \
  struct timespec g = {0};            \
  g.tv_nsec = x*1000*1000;            \
  nanosleep(&g, NULL);                \
}

#define THREAD_COUNT  32
#define ESCP_MSG_COUNT 16384
#define ESCP_MSG_SZ    128

extern uint64_t verbose_logging;

#define DBV(x, ...) {}
// #define DBV DBG

#define DBG(x, ...) if (verbose_logging) {               \
    char bu[2200];                                       \
    snprintf( bu, 2100, "[DBG] " x "\n", ##__VA_ARGS__ );\
    dtn_log( bu );                                       \
  }

#define NFO(x, ...) {                                    \
    char bu[2200];                                       \
    snprintf( bu, 2100, "[NFO] " x "\n", ##__VA_ARGS__ );\
    dtn_log( bu );                                       \
  }

#define ERR(x, ...) {                                    \
    char bu[2200];                                       \
    snprintf( bu, 2100, "[ERR] " x "\n", ##__VA_ARGS__ );\
    dtn_error( bu );                                     \
  }

#define VRFY(x, a, ...) if (!(x))  {                       \
  char b[1024];                                            \
  if (errno) {                                             \
    snprintf(b, 1000, "[ERR] " a " [%s:%d]: %s\n",         \
      ##__VA_ARGS__, __FILE__, __LINE__, strerror(errno)); \
  } else {                                                 \
    snprintf(b, 1000, "[ERR] " a " [%s:%d]\n",             \
      ##__VA_ARGS__, __FILE__, __LINE__);                  \
  }                                                        \
  dtn_error(b);                                            \
  usleep(1500000);                                         \
  if (verbose_logging) abort();                            \
  exit(-1);                                                \
}

struct fc_info_struct {
  uint64_t state;
  uint64_t file_no;
  uint64_t bytes;
  uint32_t crc;
  uint32_t completion;
  uint64_t blocks;
  uint64_t pad2[3];
};

struct dtn_args {
  bool do_server;
  bool do_ssh;
  bool do_crypto;
  bool do_hash;
  bool do_preserve;
  bool nodirect;
  bool recursive;
  uint8_t ip_mode;

  int logging_fd;
  int mtu;
  int block;
  int flags;
  int QD;

  int compression;
  int sparse;

  int64_t disable_io;
  uint64_t pacing;

  char* io_engine_name;
  int io_engine;
  unsigned int window;

  uint64_t session_id;
  uint8_t crypto_key[16];

  bool do_affinity;
  int cpumask_len;
  uint8_t cpumask_bytes[32];
  uint64_t nodemask;

  int sock_store_count;
  struct sockaddr_storage sock_store[THREAD_COUNT] __attribute__ ((aligned(64)));

  struct file_object *fob __attribute__ ((aligned(64)));
  int thread_id __attribute__ ((aligned(64)));
  int thread_count __attribute__ ((aligned(64)));
  uint16_t active_port __attribute__ ((aligned(64)));


  uint64_t debug_claim __attribute__ ((aligned(64)));
  uint64_t debug_count __attribute__ ((aligned(64)));
  uint8_t  debug_buf[ESCP_MSG_SZ*ESCP_MSG_COUNT];
  uint64_t debug_poison;

  uint64_t msg_claim   __attribute__ ((aligned(64)));
  uint64_t msg_count   __attribute__ ((aligned(64)));
  uint8_t  msg_buf[ESCP_MSG_SZ*ESCP_MSG_COUNT];
  uint64_t msg_poison;

  uint64_t bytes_io     __attribute__ ((aligned(64)));
  uint64_t files_closed __attribute__ ((aligned(64)));
  uint64_t files_open   __attribute__ ((aligned(64)));

};
extern struct dtn_args* ESCP_DTN_ARGS;
extern uint64_t ESCP_DROPPED_MSG;

static inline void dtn_log( char* msg ) {
  int log_idx = __sync_fetch_and_add( &ESCP_DTN_ARGS->debug_claim, 1 );
  log_idx = (log_idx % ESCP_MSG_COUNT) * ESCP_MSG_SZ;

  strncpy ( (char*) &ESCP_DTN_ARGS->debug_buf[log_idx], msg, ESCP_MSG_SZ );
  (&ESCP_DTN_ARGS->debug_buf[log_idx])[ESCP_MSG_SZ-1]=0;

  __sync_fetch_and_add( &ESCP_DTN_ARGS->debug_count, 1 );
  return;
}

static inline void dtn_error( char* msg ) {
  int log_idx = __sync_fetch_and_add( &ESCP_DTN_ARGS->msg_claim, 1 );
  log_idx = (log_idx % ESCP_MSG_COUNT) * ESCP_MSG_SZ;


  strncpy ( (char*) &ESCP_DTN_ARGS->msg_buf[log_idx], msg, ESCP_MSG_SZ );
  (&ESCP_DTN_ARGS->msg_buf[log_idx])[ESCP_MSG_SZ-1]=0;
  __sync_fetch_and_add( &ESCP_DTN_ARGS->msg_count, 1 );

  openlog("EScp", LOG_NDELAY|LOG_CONS|LOG_PID|LOG_PERROR, LOG_USER);
  syslog(LOG_CRIT, "%s", msg);
  closelog();

  write( STDERR_FILENO, msg, strnlen(msg, ESCP_MSG_SZ) );
  return;
}

struct fc_info_struct* fc_pop();

struct dtn_args* args_new () ;
void affinity_set ( struct dtn_args* args, int id );
// struct sockaddr_storage dns_lookup( char*, char* );
char* dtn_log_getnext();
char* dtn_err_getnext();
char* human_write(uint64_t number, bool is_bytes);

#endif
