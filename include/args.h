#ifndef __ARGS_H__
#define __ARGS_H__

#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>

#include <stdio.h>
#include <sys/time.h>

// THREAD_COUNT must be a power of 2
#define THREAD_COUNT 32
#define THREAD_MASK (THREAD_COUNT-1)

#define DEFAULT_PORT 2222
#ifndef O_VERSION
#define O_VERSION "NA"
#endif

#define FILE_LIST_SZ 8192

extern uint64_t verbose_logging;
extern uint64_t quiet;
extern uint64_t managed;
extern int      dtn_logfile;

static inline void dtn_log( char* msg ) {
  int len = strlen(msg);

  if (!managed)
    if (write( STDERR_FILENO, msg, len ));

  if (dtn_logfile) {
    char buf[2300];

    struct timeval tv;
    gettimeofday(&tv,NULL);
    uint64_t tim = tv.tv_sec * 1000000 + tv.tv_usec;

    len = sprintf ( buf, "%zX %s", tim, msg );
    if (write( dtn_logfile, buf, len ));
  }
  return;
}

#ifdef EBUG_VERBOSE
// Debug, Verbose. For all those pesky debug statements that really
// shouldn't exist in normal code.  (eXtra LOG)
#warning "Excessive logging is configured"
#define XLOG(x, ...) {                                    \
    char bu[2200];                                       \
    snprintf( bu, 2100, "[XLG] " x "\n", ##__VA_ARGS__ );\
    dtn_log( bu );                                       \
  }
#else
#define XLOG(x, ...) {}
#endif

// #define DBV(x, ...) {}
#define DBV DBG

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

#define MMSG(x, ...) if (managed) {                      \
    char bu[1024];                                       \
    int cnt;                                             \
    cnt = snprintf(bu, 1024, x "\n", ##__VA_ARGS__ );    \
    if (cnt >= 1)                                        \
      if (write(1, bu, cnt)) {};                         \
    dtn_log( bu );                                       \
  }

#define VRFY(x, a, ...) if (!(x))  {                       \
  char b[1024];                                            \
  int eno=1,sz;                                            \
  (void) sz;                                               \
  if (errno) {                                             \
    sz=snprintf(b, 1000, "Err: " a " [%s:%d]: %s\n",       \
      ##__VA_ARGS__, __FILE__, __LINE__, strerror(errno)); \
      eno = errno;                                         \
  } else {                                                 \
    sz=snprintf(b, 1000, "Err " a " [%s:%d]\n",            \
      ##__VA_ARGS__, __FILE__, __LINE__);                  \
  }                                                        \
  if (managed) {                                           \
    MMSG("ABRT\n%s", b);                                   \
  }                                                        \
  quiet = false;                                           \
  dtn_log( b );                                            \
  if (dtn_logfile) { close(dtn_logfile); dtn_logfile=0; }  \
  exit(eno);                                               \
}

struct dtn_args {
  bool do_server;
  bool no_direct;
  bool do_ssh;
  bool do_crypto;
  bool do_hash;
  bool managed;
  int file_count;
  int host_count;
  char* host_name[THREAD_COUNT];
  int mtu;
  int block;
  int flags;
  int QD;
  int disable_io;
  int persist;
  char* io_engine_name;
  int io_engine;
  unsigned int window;
  uint64_t io_bytes;
  uint64_t session_id;
  uint8_t crypto_key[16];
  struct file_object *fob;
  struct sockaddr_storage sock_store[THREAD_COUNT];

  char** opt_args;
  int opt_count;

  int thread_id __attribute__ ((aligned(64)));
  int thread_count __attribute__ ((aligned(64)));
};

struct statcntrs {
  uint64_t total_rx;
  uint64_t start_time;
  uint64_t zc;
  uint64_t reg;
  uint64_t bytes_zc;
  uint64_t bytes_read;
} __attribute__ ((aligned(64))) ;

extern struct statcntrs stat_cntr[THREAD_COUNT];
struct dtn_args* args_get ( int argc, char** argv );
char* human_write(uint64_t number, bool is_bytes);

void affinity_set ();

#endif
