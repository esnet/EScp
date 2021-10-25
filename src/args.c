#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>

#include <libgen.h>
#include <numaif.h>
#include <sched.h>

#include <byteswap.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "args.h"
#include "file_io.h"

uint64_t verbose_logging=0;
uint64_t quiet =0;
uint64_t managed =0;
int      dtn_logfile=0;

struct statcntrs stat_cntr[THREAD_COUNT] __attribute__ ((aligned(64))) ;

#pragma GCC diagnostic ignored "-Wmultichar"
char si_prefix[] = " KMGTPE";

void affinity_set ( struct dtn_args* args ) {
  int i=0;

  if (!args->do_affinity)
    return;

  cpu_set_t set;
  CPU_ZERO(&set);

  for (i=0; i<(args->cpumask_len*8); i++) {
    if (args->cpumask_bytes[i/8] & ( 1 << (i & 0x7) ))
      CPU_SET( i, &set );
  }

  set_mempolicy( MPOL_BIND, &args->nodemask, 64 );
  if ( sched_setaffinity(0, sizeof(set), &set) ) {
    perror("Setting CPU Affinity");
    exit(-1);
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

struct sockaddr_storage lookup( char* arg) {
  struct sockaddr_storage ret;
  int res;
  struct addrinfo *result;
  char *port, *host=arg;
  char buf [16];

  VRFY(host, "Bad host argument");

  // Use '/' as a seperator because ':' conflicts with IPv6 addresses
  port = strrchr( host, '/' );
  if (port) {
    port[0] = 0;
    port++;
  } else {
    sprintf(buf, "%d", DEFAULT_PORT);
    port = buf;
  }

  while (host[0] == ' ')
    host++;

  VRFY( (res=getaddrinfo(host, port, NULL, &result)) == 0,
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

void usage_full() {
  printf("\n"
"   -b SZ          I/O block SZ for network transfer and disk\n"
"   -c HOST/PORT   Connect to HOST(/PORT), can specify multiple times\n"
"   -h, --help     Display help\n"
"   -f FILE        read/write from FILE, specify multiple times\n"
"   -s             Run as server\n"
"   -t THREAD      Number of worker threads\n"
"   -D             Queue Depth\n"
"   -X [SZ]        Only do file I/O; Specify SZ to write file; omit to read\n"
"   --cpumask MASK Run on MASK CPU's, hex\n"
"   --engine  NGEN I/O engine NGEN: Posix, uring, dummy, shmem\n"
"   --logfile FILE Log to logfile FILE\n"
"   --managed      Enable managed mode\n"
"   --memnode MASK Use memory MASK matching node, hex\n"
"   --mtu     SZ   Request SZ Maximum TCP Segment\n"
"   --nodirect     Disable direct mode\n"
"   --quiet        Disable messages\n"
"   --verbose      Display verbose/debug messages\n"
"   --version      Display version\n"
"   --license      Display License\n"
"   --window  SZ   Request SZ TCP Window\n"
  "\n");


}

struct dtn_args* args_get ( int argc, char** argv ) {
  char* cpumask_str=0;
  const struct option long_options[] =
    {
      /* These options set a flag. */
      {"cpumask",  required_argument, 0, 'cpum'},
      {"help",     no_argument,       0, 'help'},
      {"engine",   required_argument, 0, 'ngen'},
      {"managed",  no_argument,       0, 'mgmt'},
      {"memnode",  required_argument, 0, 'memn'},
      {"mtu",      required_argument, 0,  'mtu'},
      {"nodirect", no_argument,       0, 'nodi'},
      {"quiet",    no_argument,       0, 'qiet'},
      {"verbose",  no_argument,       0, 'verb'},
      {"license",  no_argument,       0, 'lice'},
      {"version",  no_argument,       0, 'vers'},
      {"window",   required_argument, 0, 'wind'},
      {"logfile",  required_argument, 0, 'logf'},
      {0, 0, 0, 0}
    };

  static struct dtn_args args;
  memset( &args, 0, sizeof(args) );
  args.mtu=8204;
  args.window=512*1024*1024;
  int c, i;

  while ((c = getopt_long( argc, argv, ":sc:hf:t:xqX:b:D:",
                           long_options, NULL ) ) > 0) {
    switch(c) {
      case 'lice':
      case 'ecil':
        printf(
"ESnet Secure Copy (EScp) Copyright (c) 2021, The Regents of the\n"
"University of California, through Lawrence Berkeley National Laboratory\n"
"(subject to receipt of any required approvals from the U.S. Dept. of\n"
"Energy). All rights reserved.\n"
"\n"
"Redistribution and use in source and binary forms, with or without\n"
"modification, are permitted provided that the following conditions are met:\n"
"\n"
"(1) Redistributions of source code must retain the above copyright notice,\n"
"this list of conditions and the following disclaimer.\n"
"\n"
"(2) Redistributions in binary form must reproduce the above copyright\n"
"notice, this list of conditions and the following disclaimer in the\n"
"documentation and/or other materials provided with the distribution.\n"
"\n"
"(3) Neither the name of the University of California, Lawrence Berkeley\n"
"National Laboratory, U.S. Dept. of Energy nor the names of its contributors\n"
"may be used to endorse or promote products derived from this software\n"
"without specific prior written permission.\n"
"\n"
"\n"
"THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \"AS IS\"\n"
"AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE\n"
"IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE\n"
"ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE\n"
"LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR\n"
"CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF\n"
"SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS\n"
"INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN\n"
"CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)\n"
"ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE\n"
"POSSIBILITY OF SUCH DAMAGE.\n"
"\n"
"You are under no obligation whatsoever to provide any bug fixes, patches,\n"
"or upgrades to the features, functionality or performance of the source\n"
"code (\"Enhancements\") to anyone; however, if you choose to make your\n"
"Enhancements available either publicly, or directly to Lawrence Berkeley\n"
"National Laboratory, without imposing a separate written license agreement\n"
"for such Enhancements, then you hereby grant the following license: a\n"
"non-exclusive, royalty-free perpetual license to install, use, modify,\n"
"prepare derivative works, incorporate into other computer software,\n"
"distribute, and sublicense such enhancements or derivative works thereof,\n"
"in binary and source code form.\n");
        exit(0);


        break;

      case 'verb':
      case 'brev':
        verbose_logging++;
        break;
      case 'q':
      case 'qiet':
      case 'teiq':
        quiet=true;
        verbose_logging=0;
        break;
      case 'cpum':
      case 'mupc':
        args.do_affinity = true;
        cpumask_str = optarg;
        break;
      case 'memn':
      case 'nmem':
        sscanf(optarg, "%zX", &args.nodemask);
        break;
      case 'mgmt':
      case 'tmgm':
        quiet = true;
        verbose_logging= false;
        managed = true;
        args.managed = true;
        break;
      case 'ngen':
      case 'negn':
        args.io_engine_name = optarg;
        break;
      case 'nodi':
      case 'idon':
        args.no_direct = true;
        break;
      case 'vers':
      case 'srev':
        printf("%s Built on %s %s\n", O_VERSION, __DATE__, __TIME__);
        exit(0);
      case 'mtu':
      case 'utm':
        sscanf( optarg, "%d", &args.mtu );
        args.mtu += 12;
        break;
      case 'wind':
      case 'dniw':
        sscanf( optarg, "%d", &args.window );
        break;
      case 'logf':
      case 'fgol':
        if (!dtn_logfile) {
          dtn_logfile = open( optarg, O_WRONLY|O_CREAT|O_APPEND, 0644 );
          VRFY( dtn_logfile>0, "opening logfile '%s': %s\n",
                optarg, strerror(errno));
          DBG("Logging to file %s", optarg);
        }
        break;
      case 'c':
        args.host_name[args.host_count++] = optarg;
        break;
      case 'D':
        sscanf(optarg, "%d", &args.QD);
        break;
      case 's':
        args.do_server=true;
        break;
      case 'help':
      case 'pleh':
      case 'h':
        printf("usage: %s -shx [-f file] [-t threads] [-c host] "
               "[-b block_sz] [-X file_sz ]\n", argv[0]);
        usage_full();
        exit(0);
        break;
      case 'f':
        VRFY ( file_add( optarg, 0 ) == 0, "adding file" );
        break;
      case 'b':
        args.block =human_read(optarg);
        break;
      case 't':
        args.thread_count=atol(optarg);
        break;
      case 'x': //
        args.disable_io++;
        break;
      case 'X': // File IO only
        args.io_bytes=human_read(optarg);
        args.io_bytes &= ~4095;

        if (!args.io_bytes && (optind <= argc))
          optind--;

        args.disable_io+=11;
        break;
      case ':':
        switch (optopt) {
          case 'X':
            args.disable_io+=11;
            continue;
          default:
            VRFY( 0, "option -%c is missing a required argument", optopt );
        }
      default:
        VRFY( 0, "Argument '%c' not understood", c );
        exit(-1);
    }
  }

  if (args.do_affinity) {
    int sig[2] = {0,0};
    int len = strlen(cpumask_str);

    VRFY( strlen(cpumask_str) < 64, "assert (cpumask < 64) failed" );

    if (len > 32)
      len = 32;

    args.cpumask_len = (len+1) / 2;

    for ( i=len; i>0 ; i-=2 ) {
      int byte, pos;
      pos = i - 2;
      if (pos < 0)
        sscanf( cpumask_str, "%01x", &byte );
      else
        sscanf( cpumask_str+pos, "%02x", &byte );

      args.cpumask_bytes[(i-1)/2] = byte;
    }

    ((uint64_t*) args.cpumask_bytes)[0] = bswap_64(((uint64_t*) args.cpumask_bytes)[0]);
    ((uint64_t*) args.cpumask_bytes)[1] = bswap_64(((uint64_t*) args.cpumask_bytes)[1]);


    if (args.cpumask_len < 8) {
      ((uint64_t*) args.cpumask_bytes)[0] >>= (( 8 - args.cpumask_len)*8);
      sig[0] = len;
    }
    else if (args.cpumask_len < 16) {
      ((uint64_t*) args.cpumask_bytes)[1] >>= (( 16 - args.cpumask_len)*8);
      sig[0] = 16;
      sig[1] = len - 16;
    }

    DBG("cpumask is %*.zX%*.zX (bytes=%d)",
      sig[0], ((uint64_t*)args.cpumask_bytes)[0],
      sig[1], ((uint64_t*)args.cpumask_bytes)[1],
      args.cpumask_len);
    DBG("Nodemask is %zX", args.nodemask );
  }

  if (!args.block)
    args.block = 1 << 20;

  args.block = args.block & ~((1 << 12)-1);
  args.block = args.block < (1<<12) ? 1 << 12 : args.block;

  if ( !args.no_direct )
    args.flags |= O_DIRECT;

  if (args.io_bytes || args.do_server) {
    DBG("append write flags O_WRONLY | O_CREAT | O_TRUNC");
    args.flags |= O_WRONLY | O_CREAT | O_TRUNC ;
  }

  if (!args.QD)
    args.QD=4;

  if ( args.io_engine_name ) {
    if ( !strncasecmp( args.io_engine_name, "posix", 5 ) )
      args.io_engine = FIIO_POSIX;
    else if ( !strncasecmp( args.io_engine_name, "uring", 5 ) )
      args.io_engine = FIIO_URING;
    else if ( !strncasecmp( args.io_engine_name, "dummy", 5 ) )
      args.io_engine = FIIO_DUMMY;
    else if ( !strncasecmp( args.io_engine_name, "shmem", 5 ) )
      args.io_engine = FIIO_SHMEM;
    else
      VRFY(0, "bad engine '%s'. ngen = posix,uring,dummy", args.io_engine_name);
  } else {
    args.io_engine_name="posix";
    args.io_engine = FIIO_POSIX;
  }


  for (i=0; i<args.host_count; i++)
    args.sock_store[i] = lookup(args.host_name[i]);

  if ( !args.thread_count )
    args.thread_count=4;

  DBG("MTU set to %d (12 bytes added for TCP headers)", args.mtu );
  DBG("TCP window sz is %d", args.window );
  DBG("engine=%s, QD=%d, BS=%d", args.io_engine_name, args.QD, args.block );
  if (args.thread_count>1)
    DBG("Thread count set to %d", args.thread_count );

  DBG("Logging options are V=%zd M=%zd Q=%zd", verbose_logging, managed, quiet);

  VRFY( args.thread_count<=THREAD_COUNT, "increase pre-compiled thread_count" );

  if ( optind < argc ) {
    args.opt_args = malloc( (argc-optind) * sizeof(char*) );
    VRFY( (argc-optind) < 24, "opt arg count exceeded" );
  }

  for (i=optind; i<argc; i++) {
    args.opt_args[args.opt_count++] = argv[i];
  }

  return &args;
}


