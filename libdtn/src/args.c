#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>

#include <libgen.h>
#include <sched.h>
#include <sys/syscall.h>
#include <linux/mempolicy.h>

#include <byteswap.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "args.h"
#include "file_io.h"

uint64_t verbose_logging=0;
struct dtn_args* ESCP_DTN_ARGS;
uint64_t ESCP_DROPPED_MSG=0;

#pragma GCC diagnostic ignored "-Wmultichar"
char si_prefix[] = " KMGTPE";

void affinity_set ( struct dtn_args* args, int id ) {
  int i=0;

  if (!args->do_affinity)
    return;

  cpu_set_t set;
  CPU_ZERO(&set);

  uint64_t mask = 0;

  for (i=0; i<(args->cpumask_len*8); i++) {
    if (args->cpumask_bytes[i/8] & ( 1 << (i & 0x7) )) {
      CPU_SET( i, &set );
      mask |= 1 << i;
    }
  }

  DBG("[%2d] Set CPU affinity to %lX", id, mask);

  // Instead of: set_mempolicy( MPOL_BIND, &args->nodemask, 64 );
  int ret = syscall(SYS_set_mempolicy, MPOL_BIND, &args->nodemask, 64);
  VRFY( ret, "syscall set_mempolicy" );

  if ( sched_setaffinity(0, sizeof(set), &set) ) {
    VRFY(0, "[%2d] Setting CPU Affinity", id);
  }
}


uint64_t human_read ( char* str ) {
  uint64_t number;
  int i;
  char postfix;

  sscanf( str, "%zd%c", &number, &postfix );
  if (postfix && number) {
    if (postfix > 0x60)
      postfix -= 0x20;

    for (i=0; i<sizeof(si_prefix); i++) {
      if (postfix == si_prefix[i])
        break;
    }

    VRFY( i<sizeof(si_prefix), "Unit '%c' not understood", postfix );

    number *= 1ULL << (10 * i);
  }

  return number;
}

char* human_write(uint64_t number, bool is_bytes) {
  int i;
  float n = number;
  static __thread char output[32];
  static __thread int count=0;
  char* ptr;

  if ( ! is_bytes )
    n *= 8;

  for (i=0; i<strlen(si_prefix); i++) {
    if (n > 999) {
      if (is_bytes)
        n /= 1024;
      else
        n /= 1000;
    } else
      break;
  }

  if (i && (n<1))
    n = 1;

  ptr = &output[ ((count++&3) * 8) ];
  sprintf( ptr, "%5.1f %c", n, si_prefix[i] );
  return ptr;

}

struct sockaddr_storage dns_lookup(struct dtn_args* args, char* host, char* port) {
  struct sockaddr_storage ret;  // Function not thread safe
  int res;
  struct addrinfo *result;
  struct addrinfo hints = {0};

  DBG( "do getaddrinfo %s %s", host, port)

  hints.ai_family = AF_UNSPEC;
  if ( args->ip_mode ) {
    if (args->ip_mode == 1)
      hints.ai_family = AF_INET;
    if (args->ip_mode == 2)
      hints.ai_family = AF_INET6;
  }

  hints.ai_socktype = SOCK_STREAM;

  if (strlen(host) < 1) {
    errno=0;
    uint16_t p = strtol( port, NULL, 10 );
    VRFY(errno == 0, "Failed to convert port=%s", port);

    memset (&ret, 0, sizeof(struct sockaddr_in));
    ((struct sockaddr_in*)&ret)->sin_family = AF_INET;
    ((struct sockaddr_in*)&ret)->sin_port = htons(p);

    NFO( "connect/bind to INADDR_ANY:%s", port );
    return ret;
  }

  VRFY(host, "Bad host argument");

  VRFY( (res=getaddrinfo(host, port, &hints, &result)) == 0,
    "Host lookup failed (host='%s',port=%s); %s", host, port, gai_strerror(res) );

  memcpy( &ret, result->ai_addr, result->ai_addrlen );
  freeaddrinfo(result);

  {
    struct sockaddr_in* s;
    struct sockaddr_in6* s6;
    char buf[512];
    int port;

    s = (void*) &ret;
    s6 = (void*) &ret;

    if (s->sin_family == AF_INET) {
      inet_ntop( AF_INET, &s->sin_addr, buf, sizeof(struct sockaddr_in) );
      port = s->sin_port;
    } else {
      inet_ntop( AF_INET6, &s6->sin6_addr, buf, sizeof(struct sockaddr_in6) );
      port = s6->sin6_port;
    }

    NFO( "connect/bind to %s:%d", buf, ntohs(port) );
  }

  return ret;
}

struct dtn_args* args_new () {
  int sz = ((sizeof( struct dtn_args) + 1)/64) * 64;
  struct dtn_args* args = aligned_alloc(64, sz);

  memset (args, 0, sz);

  args->flags = O_DIRECT;
  args->msg_poison   = 0xDEADC0DE;
  args->debug_poison = 0xDEADC0DE;
  ESCP_DTN_ARGS = args;

  return args;
}


