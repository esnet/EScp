#define _GNU_SOURCE

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <execinfo.h>

#include <pthread.h>
#include <fcntl.h>

#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <libgen.h>
#include <numaif.h>
#include <sched.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <linux/tcp.h>

#include <isa-l_crypto.h>

#include "file_io.h"
#include "args.h"

#pragma GCC diagnostic ignored "-Wmultichar"

int shutdown_in_progress __attribute__ ((aligned(64))) = 0 ;
int thread_id __attribute__ ((aligned(64))) = 0 ;
pthread_t thread[THREAD_COUNT];

int did_session_init __attribute ((aligned(64))) = false ;
static struct sess_info sess;
extern char* file_list[];
extern uint64_t file_count;
extern uint64_t file_completed;

int crash_fd;

struct tx_args {
  struct dtn_args* dtn;
  struct sockaddr_storage* sock;
  int persist;
};

struct rx_args {
  struct dtn_args* dtn;
  int conn;
};

struct network_obj {
  int socket;
  int do_crypto;
  int id;
  struct dtn_args* dtn;

  uint32_t block;

  struct gcm_context_data gctx;
  struct gcm_key_data gkey;
  union {
    uint8_t iv[16];
    struct {
      uint32_t iv_salt;
      uint64_t iv_incr;
      uint32_t iv_one;
    };
  };
  void* token;
  struct file_object* fob;

  uint8_t buf[2048];
};

struct crypto_session_init {
  uint16_t hdr_type;
  uint16_t hdr_sz;
  uint8_t iv[12];
  uint8_t key[20];
  uint8_t hash[16];
} __attribute__ ((packed)) ;

struct crypto_hdr {
  uint16_t hdr_type;
  uint16_t magic;
  uint32_t hdr_sz;
  uint64_t iv;
} __attribute__ ((packed)) ;

uint64_t global_iv=0;

struct statcntrs* aggregate_statcntrs( ) {
  static struct statcntrs copy;
  memset( &copy, 0, sizeof(copy) );

  {
    int i=0;
    struct statcntrs copy_temp;

    for (i=0; i<16; i++) {
      memcpy( &copy_temp, &stat_cntr[i], sizeof(copy) );
      copy.total_rx   += copy_temp.total_rx;
      copy.zc         += copy_temp.zc;
      copy.reg        += copy_temp.reg;
      copy.bytes_zc   += copy_temp.bytes_zc;
      copy.bytes_read += copy_temp.bytes_read;
      if (copy_temp.start_time > copy.start_time)
        copy.start_time = copy_temp.start_time;
    }
  }
  return &copy;
}

void* status_bar ( void* arg ) {
  struct timeval now;
  struct statcntrs* copy;

  uint64_t last_rx=0, last_time=0, now_time;

  while ( __sync_fetch_and_add ( &shutdown_in_progress, 0 ) == 0 ) {
    char buf[512];
    int sz;

    usleep ( 500000 );
    gettimeofday( &now, NULL );
    copy = aggregate_statcntrs();

    if (!last_time) {
      last_time = copy->start_time;
    }

    now_time = now.tv_sec * 1000000 + now.tv_usec ;

    {
      uint64_t delta = now_time - last_time;
      uint64_t bytes = (copy->total_rx - last_rx) * 1000000 ;

      sz = sprintf(buf, "\r %siB, %sbit/s    ",
         human_write( copy->total_rx, true ),
         human_write( bytes/delta, false )
       );
    }

    if (!quiet)
      if (write( 1, buf, sz )) {};

    if (managed) {
      MMSG("STAT\n%zd", copy->total_rx);
    }

    last_rx = copy->total_rx;
    last_time = now_time;

  }
  DBG("[main] Status bar is finished");

  return 0;
}

int xit( int code ) {
  // X-it: dismount horse/leave ship

  if (code == 0) {
    DBG("Exiting... ");
    MMSG("XIT");
  } else {
    DBG("Aborted... ");
    MMSG("ABRT");
  }


  if (dtn_logfile) {
    close(dtn_logfile);
  }

  exit(code);
}

static inline int io_fixed(
    int fd, void* buf, int sz, ssize_t (*func) (int, void*, size_t) )
{
  int bytes_total=0, bytes_read;
  uint8_t* b=buf;

  while ( bytes_total < sz ) {
    bytes_read = func( fd, b + bytes_total, sz-bytes_total );
    if (bytes_read < 1)
      return bytes_read;
    bytes_total += bytes_read;
  }

  return bytes_total;
}

static inline int read_fixed( int fd, void* buf, int sz ) {
  return io_fixed( fd, buf, sz, read );
}

static inline int write_fixed ( int fd, void* buf, int sz ) {
  return io_fixed( fd, buf, sz, (ssize_t (*) (int, void*, size_t)) write );
}


int read_newline( int fd, void* buf, int sz ) {
  int i=0, res;
  uint8_t* b = buf;
  for (i=0; i< sz; i++) {
    res = read( fd, b+i, 1 );
    if ( res < 0 )
      return res;
    if (res == 0)
      return i;
    if ( b[i]  == '\n' ) {
      b[i] = 0;
      return i;
    }
  }

  return i;
}


struct network_obj* network_inittx ( int socket, struct dtn_args* dtn ) {
  static __thread struct network_obj knob;
  memset( &knob, 0, sizeof(knob) );
  struct crypto_session_init csi;
  uint8_t iv[16];
  uint8_t key[20];

  knob.socket = socket;
  if (!dtn->do_crypto) {
    return &knob;
  }

  knob.do_crypto = true;

  aes_gcm_pre_128( dtn->crypto_key, &knob.gkey );

  /* Send crypto session init HDR */
  csi.hdr_type = FIHDR_CINIT;
  csi.hdr_sz = sizeof( struct crypto_session_init );

  ((uint32_t*) csi.iv)[0] = (uint32_t) dtn->session_id ;
  ((uint64_t*) (&csi.iv[4])) [0] = __sync_fetch_and_add (&global_iv, 1 );

  memcpy( iv, csi.iv, 12 );
  ((uint32_t*) (&iv[12])) [0] = 1;

  file_randrd( key, 20 );
  memcpy( csi.key, key, 20 );

  aes_gcm_enc_128( &knob.gkey, &knob.gctx,
                   csi.key, csi.key, 20, csi.iv, (uint8_t*) &csi, 16, csi.hash, 16);

  /*  hdr_type | hdr_sz | iv | new key | new iv  salt |  Hash
   *    2      |   2    | 12 |  16     | 4            |  16
   *          AAD            | Encrypted              |
   */

  aes_gcm_pre_128( key, &knob.gkey );
  memcpy( knob.iv, (uint32_t*)(&key[16]), 4 );
  memset( key, 0, 20 );

  VRFY( write( socket, &csi, sizeof(csi) ) == sizeof(csi), );

  return &knob;
}

struct network_obj* network_initrx ( int socket,
                                     uint8_t* buf,
                                     struct dtn_args* dtn ) {

  static __thread struct network_obj knob = {0};
  struct network_obj temp = {0};
  struct crypto_session_init csi;
  uint8_t hash[16];
  uint8_t iv[16];

  memcpy ( &csi, buf, 16 );

  knob.socket = socket;

  if ( !dtn->do_crypto ) {
    return &knob;
  }

  VRFY(csi.hdr_sz == sizeof(struct crypto_session_init), "CSI hdr");
  VRFY(read_fixed( socket, ((uint8_t*)&csi)+16, csi.hdr_sz-16 )>1, "CSI read");

  aes_gcm_pre_128( dtn->crypto_key, &temp.gkey );

  memcpy( iv, csi.iv, 12 );
  ((uint32_t*)(iv+12))[0] = 1;

  aes_gcm_dec_128( &temp.gkey, &temp.gctx, csi.key, csi.key, 20, iv,
                   (uint8_t*) &csi, 16, hash, 16 );
  VRFY( memcmp( hash, csi.hash, 16 ) == 0, "Bad Hash" );

  aes_gcm_pre_128( csi.key, &knob.gkey );
  memcpy( knob.iv, &csi.key[16], 4 );

  knob.iv_one = 1;
  knob.do_crypto = 1;

  return &knob;
}

int64_t network_recv( struct network_obj* knob, void* aad ) {

  struct crypto_hdr* chdr = (struct crypto_hdr*) aad;
  struct file_info* fi = (struct file_info*) knob->buf;

  static __thread bool did_init = false;
  int64_t bytes_read=16;

  if (knob->do_crypto) {
    if (chdr->hdr_sz<48) {
      return -1;
    }

    knob->iv_incr = chdr->iv;

    aes_gcm_init_128( &knob->gkey, &knob->gctx, knob->iv, aad, 16 );
    VRFY(read_fixed( knob->socket, knob->buf, 16 )>1, );
    bytes_read += 16;

    aes_gcm_dec_128_update(&knob->gkey, &knob->gctx, knob->buf, knob->buf, 16);
  } else {
    memcpy( knob->buf, aad, 16 );
  }

  if ( (fi->hdr_type == FIHDR_LONG) || (fi->hdr_type == FIHDR_END) ) {
    struct file_info_long* fil = (struct file_info_long*) fi;
    int read_sz = fil->hdr_sz - 16;
    if (read_sz) {
      bytes_read += read_sz;
      VRFY(read_sz < 2000, "hdr_sz in FIHDR_LONG is 2k+n");
      if ( read_fixed(knob->socket, knob->buf+16, read_sz) < 1 )
        return 0;
      if ( knob->do_crypto ) {
        aes_gcm_dec_128_update( &knob->gkey, &knob->gctx,
          knob->buf+16, knob->buf+16, read_sz );
      }
    }
  }

  if ( !did_init && ((fi->hdr_type ==  FIHDR_SHORT) ||
                     (fi->hdr_type ==  FIHDR_LONG)) ) {
    static struct sess_info s;
    int loop_count=0;

    while (!did_session_init) {
      if ( loop_count++ )
        usleep(1000);
      __sync_fetch_and_add( &did_session_init, 0 );
    }
    memcpy( &s, &sess, sizeof(sess) );
    knob->dtn->block  = s.block_sz;
    knob->block       = knob->dtn->block;

    knob->dtn->thread_count = s.thread_count;
    knob->fob = file_memoryinit( knob->dtn, knob->id );
    did_init=true;
    DBG("[%2d] Init IO mem ", knob->id );
  }

  if ( fi->hdr_type == FIHDR_SHORT ) {
    // Read into buffer from storage I/O
    int sz;
    uint64_t res;
    uint8_t* buffer;

    bytes_read += fi->block_sz ;

    while ( (knob->token=knob->fob->fetch(knob->fob)) == 0 ) {
        // If no buffer is available wait unti I/O engine writes out data
        knob->token = knob->fob->submit(knob->fob, &sz, &res);
        VRFY(sz > 0, "write error");
        knob->fob->complete(knob->fob, knob->token);
    }

    buffer = knob->fob->get( knob->token, FOB_BUF );

    if (read_fixed(knob->socket, buffer, fi->block_sz)<1)
      return 0;

    if ( knob->do_crypto ) {
       aes_gcm_dec_128_update( &knob->gkey, &knob->gctx,
         buffer, buffer, fi->block_sz );
    }
  }

  if ( knob->do_crypto ) {
    uint8_t computed_hash[16];
    uint8_t actual_hash[16];

    aes_gcm_dec_128_finalize( &knob->gkey, &knob->gctx, computed_hash, 16 );
    if (read_fixed(knob->socket, actual_hash, 16)<1)
      return 0;
    VRFY( memcmp(computed_hash, actual_hash, 16) == 0,
      "Bad auth tag hdr=%d", fi->hdr_type  );
    bytes_read += 16;
  }

  return bytes_read;
}


int64_t network_send (
  struct network_obj* knob, void* buf, int sz, int total, bool partial
  ) {

  struct crypto_hdr hdr;
  uint8_t hash[16] = {0};
  uint64_t sent=0, res;
  static __thread bool did_header=0;

  if (knob->do_crypto) {
    if ( !did_header ) {
      hdr.hdr_type = FIHDR_CRYPT;
      hdr.magic = 0xa7be;
      knob->iv_incr ++;
      hdr.iv = knob->iv_incr;
      hdr.hdr_sz = total + 32;

  /*  ---------+--------+----+----+--------------+----------\
   *  hdr_type | magic  | sz | IV | Payload      | HMAC     |
   *    2      |   2    | 4  |  8 | sz - 32      |  16      |
   *          AAD                 | Encrypted    | Auth tag |
   *  ---------+------------------+--------------+----------/
   */

      did_header = true;
      sent = write_fixed( knob->socket, &hdr, sizeof(hdr) );
      if ( sent < 1 )
        return sent;

      aes_gcm_init_128( &knob->gkey, &knob->gctx, knob->iv, (uint8_t*) &hdr, 16 );
    }

    aes_gcm_enc_128_update( &knob->gkey, &knob->gctx,
      buf, buf, sz );

    res = write_fixed( knob->socket, buf, sz );
    if (res < 1)
      return res;
    sent += res;

    if (! partial ) {
      aes_gcm_dec_128_finalize( &knob->gkey, &knob->gctx, hash, 16 );
      res +=  write_fixed( knob->socket, hash, 16 );

      if (res < 1)
        return res;
      sent += res;
      did_header = false;
    }

  } else {
    sent =  write_fixed( knob->socket, buf, sz );
    if (sent < 1)
      return sent;
  }

  return sent;
};



void* rx_worker( void* arg ) {
  struct timeval start;
  struct rx_args* rx = arg;
  struct dtn_args* dtn = rx->dtn;
  int incr=0, fd=0, sz;
  uint32_t id=0, rbuf;
  uint64_t file_cur=0, shutdown=0, res;

  uint64_t file_total=0;
  uint32_t crc=0;

  struct file_object* fob;
  struct file_info* fi;
  struct file_stat_type* fs=0;
  struct network_obj* knob=0;
  struct statcntrs local __attribute ((aligned(64))) ;

  uint8_t read_buf[16];



  socklen_t rbuf_sz = sizeof(rbuf) ;

  VRFY( getsockopt(rx->conn, SOL_SOCKET, SO_RCVBUF, &rbuf, &rbuf_sz) != -1,
        "SO_RCVBUF" );
  if ( rbuf != dtn->window ) {
    NFO("rcvbuf sz mismatch %d (cur) != %d (ask)", rbuf, dtn->window);
  }

  id = __sync_fetch_and_add(&dtn->thread_id, 1);

  if (id==1)
    MMSG("SESS\nAccept %d", id);

  DBG("[%2d] Accept connection", id);

  affinity_set();

  gettimeofday( &start, NULL );
  memset( &local, 0, sizeof(local) );
  local.start_time = start.tv_sec * 1000000 + start.tv_usec;

  while ( !shutdown ) {
    uint64_t read_sz=read_fixed( rx->conn, read_buf, 16 );
    VRFY (read_sz == 16, "bad read (network)" );

    if ( !knob ) {
      knob = network_initrx ( rx->conn, read_buf, dtn );
      knob->id = id;
      knob->dtn = dtn;

      VRFY(knob, "network init failed");
      if (dtn->do_crypto)
        continue;
    }

    if ( (read_sz=network_recv(knob, read_buf)) < 1 )
      break;

    fob = knob->fob;

    fi = (struct file_info*) knob->buf;
    if ( fi->hdr_type== FIHDR_CINIT )
      continue;

    if ( fi->hdr_type == FIHDR_SESS ) {
      memcpy( &sess, fi, sizeof(sess) );
      __sync_fetch_and_add( &did_session_init, 1 );
      DBG("[%2d] Establish session %016zx bs=%d tc=%d", id,
        sess.session_id, sess.block_sz, sess.thread_count );
      continue;
    }


    if ( fi->hdr_type == FIHDR_END ) {
      struct file_info_end* fi_end;
      fi_end = (struct file_info_end*) fi;
      if (fi_end->type == FIEND_SESS) {
        DBG("[%2d] Got FIHDR_END", id);
        shutdown = __sync_add_and_fetch(&shutdown_in_progress, 1);
        MMSG("SESS\nTerminate");
        continue;
      }

      if (fi_end->type == FIEND_FILE) {
        struct file_close_struct* fc;

        DBG("[%2d] Got FIHDR_FILE", id);

        if ( fi_end->hash_is_valid )
          fc = file_close( fob, fs, file_total, crc, 0, fi_end->hash, fi_end->workers );
        else
          fc = file_close( fob, fs, file_total, crc, 0, 0, fi_end->workers );

        if (fc->flushing == fi_end->workers) {
          DBG("[%2d] File %d is complete", id, fi_end->file_no);
          VRFY( (fc->given_crc == fc->hash),
            "Hash for %d is BAD %08X!=%08X !", fi_end->file_no,
            fc->given_crc, fc->hash);
        } else {
          DBG("[%2d] Not closing file %d, because not complete", id,
               fi_end->file_no);

        }

        crc=0;
        continue;
      }

      DBG("[%2d] Unknown FIHDR_END type", id);

      continue;
    }

    if ( fi->hdr_type == FIHDR_LONG ) {
      struct file_info_long* fi_long = (struct file_info_long*) fi;
      VRFY( fi_long->name_sz < 512, "file name is too long" );
      fi_long->bytes[fi_long->name_sz-1] = 0;

      fs = file_next( fob, fi_long->file_no, fi_long->bytes );
      VRFY( fs, "opening file" );

      if ( fi_long->file_sz &&
           posix_fallocate(fs->fd, 0, fi_long->file_sz) ) {
        perror("Allocating disk space for file\n");
        xit(-1);
      }

      file_cur = fs->file_no;
      fd = fs->fd;

      local.total_rx += read_sz;

      DBG("[%2d] Completed FIHDR_LONG", id);
      continue;
    }

    if (fi->hdr_type != FIHDR_SHORT) {
      VRFY(0, "Unkown header type %d", fi->hdr_type);
    }

    { // Do FIHDR_SHORT
      // WARN: fi->offset bit twiddling based on intel byte order

      fob->set( knob->token, FOB_OFFSET, fi->offset >> 8);
      fob->set( knob->token, FOB_SZ, fi->block_sz );

      if ( (fob->io_flags & O_DIRECT) && (fi->block_sz & 0xfff) ) {
        fob->set( knob->token, FOB_SZ,  (fi->block_sz & ~0xfff) + 0x1000 );
        fob->set( knob->token, FOB_TRUNCATE,  fi->block_sz + (fi->offset >> 8));
      }

      {
        uint8_t* buf = fob->get( knob->token, FOB_BUF );
        crc ^= file_hash( buf, fi->block_sz,
          ((fi->offset>>8)+sess.block_sz-1LL)/sess.block_sz );
      }

      DBG("[%2d] Do FIHDR_SHORT crc=%08x fn=%d offset=%zx", id, crc, fi->file_no, fi->offset>>8);


      file_total += fi->block_sz;

      while ( !fd || (file_cur != fi->file_no) ) {
        // Fetch the file descriptor associated with block

        VRFY( fi->file_no, "bad file_no" );

        fs = file_wait( fob, fi->file_no );
        DBG("[%2d] Got fd=%d for fn=%d", id, fs->fd, fi->file_no);

        VRFY( fs->fd, "bad file descriptor %d", fs->fd );

        file_cur = fi->file_no;
        fd = fs->fd;
      }

      fob->set( knob->token, FOB_FD, fd );
      fob->flush( fob );
    }

    while ( (knob->token = fob->submit(fob, &sz, &res)) ) {
      VRFY(sz > 0, "write error, fd=%zd",
           (uint64_t)fob->get(knob->token, FOB_FD));
      fob->complete(fob, knob->token);
    }


    local.total_rx += read_sz;
    local.reg += 1;
    local.bytes_read += read_sz;

    if ( (incr++ & 0xff) == 0xff ) // Update stat counter
      memcpy( &stat_cntr[id&THREAD_MASK], &local, sizeof(local) );
  }

  memcpy( &stat_cntr[id&THREAD_MASK], &local, sizeof(local) );
  if (rx->conn > 1)
    close( rx->conn );

  {
    struct statcntrs* r;
    float duration;
    struct timeval now;
    uint64_t now_time;
    uint64_t tc;
    memcpy ( &tc, &dtn->thread_count, sizeof(tc) );

    if ( shutdown != tc ) {
      DBG("[%2d] shutdown thread", id);
      return 0;
    }

    gettimeofday( &now, NULL );
    now_time = now.tv_sec * 1000000 + now.tv_usec ;

    r = aggregate_statcntrs();
    duration = (now_time - r->start_time) / 1000000.0;

    NFO(
      "\nReceiver Report:\n"
      "Network= %siB, Elapsed= %.2fs, %sbit/s\n"
      "# of reads = %zd, bytes/read = %zd",

      human_write(r->total_rx, true),
      duration, human_write(r->total_rx/duration,false),
      r->reg, r->reg ? r->bytes_read/r->reg : 0

    );

    xit(0);
  }
  return 0;
}

void* tx_worker( void* args ) {

  struct tx_args* arg = (struct tx_args*) args;

  struct sockaddr_in* saddr = (struct sockaddr_in*) arg->sock;
  struct dtn_args* dtn = arg->dtn;
  int sock=0, id, file_no=-1;
  uint64_t offset, incr=0;
  int32_t bytes_read;
  struct statcntrs local;
  struct file_stat_type* fs=0;

  int protocol = saddr->sin_family;
  int protocol_sz = sizeof(*saddr);
  int did_work=0;

  uint32_t sbuf;
  struct file_object* fob;

  struct network_obj* knob;
  void* token;

  uint64_t file_total;
  uint32_t crc=0;

  if (protocol == AF_INET6)
    protocol_sz = sizeof(struct sockaddr_in6);

  affinity_set();

  id = __sync_fetch_and_add(&thread_id, 1);
  fob = file_memoryinit( dtn, id );
  fob->id = id;

  DBG( "[%2d] thread start", id);

  sbuf = dtn->window;
  socklen_t sbuf_sz = sizeof(sbuf);

  VRFY((sock = socket( protocol, SOCK_STREAM, 0)) != -1, "Parsing IP");
  VRFY(setsockopt(sock, IPPROTO_TCP, TCP_MAXSEG,
                  &dtn->mtu, sizeof(dtn->mtu)) != -1,);
  VRFY(setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sbuf, sbuf_sz) != -1,);
  VRFY(getsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sbuf, &sbuf_sz) != -1,);

  if (sbuf != dtn->window) {
    NFO("[%d] Requested TCP window size of %d, but got %d bytes",
        id, dtn->window, sbuf );
  }

  VRFY( -1 !=
    connect(sock, (void *)saddr, protocol_sz),
    "Connecting to remote host" );

  if (!id) {
    MMSG("SESS\nConnected");
  }

  VRFY( getsockopt( sock, IPPROTO_TCP, TCP_MAXSEG, &sbuf, &sbuf_sz) != -1, );

  if ( (sbuf != dtn->mtu) && ((sbuf+12) != dtn->mtu) ) {
    NFO("[%d] TCP_MAXSEG value is %d, requested %d", id, sbuf, dtn->mtu);
  }

  {
    struct timeval start;
    gettimeofday( &start, NULL );
    memset( &local, 0, sizeof(local) );
    local.start_time = start.tv_sec * 1000000 + start.tv_usec;
  }

  knob = network_inittx( sock, dtn );
  VRFY (knob != NULL, );

  if (!id) {
    struct sess_info s;

    s.hdr_type=FIHDR_SESS;
    s.block_sz = dtn->block;
    s.thread_count = dtn->thread_count;
    s.session_id = dtn->session_id;

    DBG("[%2d] Set session %016zx bs=%d tc=%d", id,
      s.session_id, s.block_sz, s.thread_count);
    VRFY( network_send( knob, &s, 16, 16, false ) > 0, "" );
  }

  while (1) {
    if (!fs) {
      fs = file_next( fob, 0, 0 );

      if ( !fs ) {

        DBG("[%2d] Finished reading file(s), send termination", id );
        struct file_info_end fi;
        fi.hdr_type = FIHDR_END;
        fi.hdr_sz = 16;
        fi.type = FIEND_SESS;

        VRFY( network_send( knob, &fi, 16, 16, false ) > 0, "" );
        break;
      }

      file_no = fs->file_no;
      crc= file_total= 0;
      did_work=0;
    }

    while ( (token=fob->fetch(fob)) ) {
      offset = __sync_fetch_and_add( &fs->block_offset, 1 );
      offset *= dtn->block;
      fob->set(token, FOB_OFFSET, offset);
      fob->set(token, FOB_SZ, dtn->block);

      if (!offset) {
        // We're the first to read from the file, send FIHDR_LONG:
        struct file_info_long fil;

        if (!did_work) {
          file_mark_worker( fs, fob->id );
          did_work=1;
        }

        memset( &fil, 0, sizeof(fil) );

        fil.name_sz = strlen ( fs->name );
        strncpy( (char*) fil.bytes, fs->name, 5000 );
        {
          char* tmp = basename( (void*) fil.bytes );
          fil.name_sz = strlen(tmp);
          memmove( fil.bytes, tmp, fil.name_sz );
          fil.bytes[fil.name_sz++] = 0;
        }

        fil.hdr_type = FIHDR_LONG;
        fil.hdr_sz = 32 + fil.name_sz;
        fil.file_no = file_no;

        VRFY( network_send(knob, &fil, fil.hdr_sz, fil.hdr_sz, false) > 0, );
      }

      fob->set(token, FOB_FD, fs->fd);
    }

    token = fob->submit( fob, &bytes_read, &offset );

    if (bytes_read < 1) {

      if (bytes_read == 0) { // EOF
        struct file_close_struct* fc;
        struct file_info_end fie = {0};

        DBG("[%2d] Got EOF reading fd=%d", id, fs->fd );
        if (!did_work) {
          DBG("[%2d] But not sending EOF, because I didn't do anything.", id );
          fc = file_close( fob, fs, file_total, crc, 1, 0, 0 );
          // VRFY(fc,)
          fs = 0;
          continue;
        }

        fc = file_close( fob, fs, file_total, crc, 0, 0, 0 );
        VRFY(fc,)

        fie.hdr_type = FIHDR_END;
        fie.hdr_sz = 32;
        fie.type = FIEND_FILE;
        fie.workers = fc->workers;
        fie.hash    = fc->hash;
        fie.sz      = fc->sz;
        fie.hash_is_valid = fc->did_close;
        fie.file_no = fc->file_no;

        DBG("[%2d] send file_close on %s:%d workers=%d sz=%zd",
          id, fc->fn, fc->file_no, fc->workers, fc->sz);


        VRFY( network_send( knob, &fie, fie.hdr_sz, fie.hdr_sz, false ) > 0, "" );
        fs = 0;
        continue;
      }

      VRFY( bytes_read >= 0, "Read Error" );
      NFO("read error: %s", strerror(errno) );
      return (void*) -1;
    }

    if (!did_work) {
      file_mark_worker( fs, fob->id );
      did_work=1;
    }


    file_total += bytes_read;

    local.reg += 1;
    local.bytes_read += bytes_read;
    local.total_rx += bytes_read+16;
    if (dtn->do_crypto)
      local.total_rx += 32;

    if ( (incr++ & 0xff) == 0xff )
      memcpy( &stat_cntr[id&THREAD_MASK], &local, sizeof(local) );

    {
      uint8_t* buf = fob->get( token, FOB_BUF );
      struct file_info fi = {0};
      fi.file_no = file_no;
      fi.offset = offset << 8ULL;
      fi.hdr_type = FIHDR_SHORT;
      fi.block_sz = bytes_read;

      crc ^= file_hash( buf, bytes_read, (offset+dtn->block-1LL)/dtn->block );

      VRFY( network_send(knob, &fi, 16, 16+bytes_read, true) > 0, );
      VRFY( network_send(knob, buf, bytes_read, 16+bytes_read, false) > 0, );

      fob->complete(fob, token);

      DBG("[%2d] Finish block crc=%08x fn=%d offset=%zx", id, crc, file_no, offset);
    }

    did_work=1;
  }

  memcpy( &stat_cntr[id&THREAD_MASK], &local, sizeof(local) );
  __sync_add_and_fetch(&shutdown_in_progress, 1);

  DBG("[%2d] Thread exited", id );
  return 0;
}

void tx_start(struct dtn_args* args ) {
  int i=0;
  static int did_tx_start=0;

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  static struct tx_args tx_arg[THREAD_COUNT] = {0};

  file_randrd( &args->session_id, 8 );

  if (!args->thread_count)
    args->thread_count=1;

  VRFY( args->host_count, "No host specified");

  DBG("[mgmt] tx_start: begin");

  if (did_tx_start) {
    DBG("[mgmt] tx_start: already started, returning");
    MMSG("ERR: tx_start already started");
    return;
  }

  MMSG("OKAY");

  for (i=0; i < args->thread_count; i++)  {
    tx_arg[i].sock = &args->sock_store[i%args->host_count];
    tx_arg[i].dtn = args;
    tx_arg[i].persist = args->persist;

    if (  pthread_create(
          &thread[i], &attr, tx_worker, (void*) &tx_arg[i] )
       )
    {
      VRFY( 0, "pthread_create" );
    }
  }

  did_tx_start=1;
  if (args->persist) {
    DBG("[mgmt] tx_start: Exit loop because persist specified");
    return;
  }

  DBG("[mgmt] tx_start: show status_bar");
  status_bar(NULL);

  DBG("[mgmt] tx_start: wait for workers");
  for (i=0; i < args->thread_count; i++)
    pthread_join( thread[i], NULL );

  DBG("[mgmt] tx_start: workers exited ");

  {
    struct statcntrs* r;
    float duration;
    struct timeval now;
    uint64_t now_time;
    gettimeofday( &now, NULL );
    now_time = now.tv_sec * 1000000 + now.tv_usec ;

    r = aggregate_statcntrs();
    duration = (now_time - r->start_time) / 1000000.0;

    NFO(
      "\nSender Report:\n"
      "Network= %siB, Elapsed= %.2fs, %sbit/s\n" "# of reads = %zd, bytes/read = %zd",

      human_write(r->total_rx, true),
      duration, human_write(r->total_rx/duration,false),
      r->reg, r->reg ? r->bytes_read/r->reg : 0

    );
  }

  return;
}

void* rx_start( void* fn_arg ) {
  int i=0, sock, one=1;
  struct dtn_args* args = fn_arg;
  struct sockaddr_in* saddr = (void*) &args->sock_store[0];
  int addr_sz;
  static struct rx_args rx_arg[THREAD_COUNT] = {0};

  if ( !saddr->sin_family ) {
    saddr->sin_family = AF_INET;
    saddr->sin_port = htons(DEFAULT_PORT);
    DBG("Listening on default port 0:%d", DEFAULT_PORT);
  }

  if ( saddr->sin_family == AF_INET )
    addr_sz = sizeof(struct sockaddr_in);
  else
    addr_sz = sizeof(struct sockaddr_in6);

  VRFY ( (sock = socket( saddr->sin_family, SOCK_STREAM, 0)) != -1, );

  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

  VRFY( bind(sock, (struct sockaddr*) saddr, addr_sz) != -1, "Socket Bind" );
  VRFY ( listen( sock, THREAD_COUNT ) != -1, "listening" );

  /* setsockopt(sock, SOL_SOCKET, SO_RCVLOWAT, &Block_sz, sizeof(Block_sz)) */

  {
    uint64_t rbuf = args->window;
    socklen_t rbuf_sz = sizeof(rbuf);
    if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rbuf, rbuf_sz) == -1) {
      perror("SO_RCVBUF");
      xit(errno);
    }
  }

  while(1) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    socklen_t saddr_sz = sizeof(struct sockaddr_in);

    rx_arg[i].conn = accept( sock, (struct sockaddr *) saddr, &saddr_sz );
    rx_arg[i].dtn = args;

    VRFY (rx_arg[i].conn != -1, "accepting socket" );

    if (  pthread_create(
            &thread[i], &attr, rx_worker, (void*) &rx_arg[i] )
       ) {
      perror("pthread_create");
      close(rx_arg[i].conn);
      continue;
    }

    i = (i+1) & THREAD_MASK;
  }
}

void do_management( struct dtn_args* args ) {
  bool tru=true, did_emit=false;
  char buf[1024];
  int i, res=0;
  uint32_t arg;
  uint32_t* code;

  MMSG("REDY");
  while ( managed ) {
    if (read( STDIN_FILENO, buf, 5 ) != 5) {
      DBG("Management session is closed");
      break;
    }

    code = (uint32_t*) buf;

    switch( code[0] ) {
      case 'CKEY':
      case 'YEKC':
        // Set Crypto Key ( Exactly 16 bytes in 32 hex chars )


        DBG("[mgmt] Enable Crypto");
        VRFY( (res=read_newline( STDIN_FILENO, buf, 32 )) > 0, );
        VRFY( (res=file_b64decode(buf, buf, res)) >= 16, "crypto key too short, read=%d", res );
        memcpy( args->crypto_key, buf, 16 );
        memcpy( &args->do_crypto, &tru, sizeof(bool) );
        DBG("Enabled Crypto");
        MMSG("OKAY");
        break;
      case 'RECV':
      case 'VCER':
        // Initiate client as a receiver

        DBG("[mgmt] Start RECV");
        MMSG("OKAY");
        rx_start(args);
        break;
      case 'SEND':
      case 'DNES':
        // Initiate client as a sender

        DBG("[mgmt] Start SEND");
        tx_start(args);
        break;
      case 'TEST':
      case 'TSET':
        DBG("[mgmt] Running file_iotest");
        MMSG("OKAY");
        if (!args->thread_count)
          args->thread_count=1;
        file_iotest(args);
        break;

      case 'PERS':
      case 'SREP':
        DBG("[mgmt] Enable persistent mode");
        // Enable persistent mode (don't exit after last file is complete)
        args->persist = true;
        file_persist(true);
        MMSG("OKAY");
        break;

      case 'FILE':
      case 'ELIF':
        // Add a list of files; # files <newline> [ file <newline> ] ...
        VRFY( (res=read_newline( STDIN_FILENO, buf, 1000 )) > 0, );
        sscanf(buf, "%d", &arg);
        {
          VRFY( arg < 15000, "Add files in smaller groups" );
          char** lines;
          lines = malloc( (arg+1) * sizeof (char*) );
          VRFY( lines, );

          for (i=0; i<arg; i++) {
            VRFY( (res=read_newline( STDIN_FILENO, buf, 1000 )) > 0, );

            lines[i] = malloc(res+1);
            VRFY(lines[i],);

            memcpy( lines[i], buf, res );
            lines[i][res] = 0;

            DBG("[mgmt] FILE: '%s'", buf);
          }

          file_add_bulk( lines, arg );

          for (i=0; i<arg; i++) {
            free(lines[i]);
          }
          free(lines);
        }

        MMSG("OKAY");
        break;

      case 'ABRT':
      case 'TRBA':
        DBG("[mgmt] QUIT received, exiting.");

        if (args->fob && args->fob->cleanup) {
          printf("doing cleanup\n");
          args->fob->cleanup(args->fob);
        }

        __sync_add_and_fetch(&shutdown_in_progress, 1);

        MMSG("ABRT");
        did_emit=true;

      case 'DONE':
      case 'ENOD':
        // Mark session as complete and wait for exit
        DBG("[mgmt] Join workers.");

        args->persist = false;
        file_persist(false);

        for (i=0; i < args->thread_count; i++)
          pthread_join( thread[i], NULL );

        if (!did_emit)
          MMSG("OKAY");
        return;

      case 'HASH':
      case 'HSAH':
        DBG("[mgmt] Enabling hash");

        // Primarily for debugging; This enables a modified CRC32 hash of
        // each file to verify that files are being written to disk correctly.

        args->do_hash=1;
        MMSG("OKAY");
        break;

      case 'STAT':
      case 'TATS':
        DBG("[mgmt] Enabling stats!");
        {
          pthread_t id;
          VRFY( pthread_create( &id, NULL, status_bar, NULL ) == 0, );
        }
        MMSG("OKAY");
        break;

      case 'FTOT':
      case 'TOTF':
        DBG("[mgmt] Computing file sizes");

        // Requires: (int) file to start at
        // Return metrics; files <newline> total bytes

        VRFY( (res=read_newline( STDIN_FILENO, buf, 32 )) > 0, );

        i = atoi( buf );
        DBG("[mgmt] Got starting from file %d", i);

        file_compute_totals(i);


        break;

      case 'CHDR':
      case 'RDHC':
        // CHeDeR
        VRFY( (res=read_newline( STDIN_FILENO, buf, 512)) > 0, );

        {
          int res;
          struct stat st;
          res = stat(buf, &st);

          if (res) {
            if ( errno == ENOENT ) {
              // File does not exist, we are clear to overwrite
              MMSG("FILE");
              break;
            }
            MMSG("ABRT\nCouldn't stat: %s", strerror(errno) );
          }

          switch (st.st_mode & S_IFMT) {
            case S_IFDIR:
              if (chdir(buf)) {
                MMSG("ABRT\nCouldn't chdir: %s", strerror(errno));
              } else {
                MMSG("CHDR");
              }
              break;
            case S_IFREG:
              MMSG("FILE");
              break;
            case S_IFLNK:
              MMSG("ABRT\nTarget is a symlink. Symlinks are not supported\n");
              break;
            default:
              MMSG("ABRT\nTarget is special device");
          }

        }
        break;

      case 'VERS':
      case 'SREV':
        MMSG("0.1");
        return;

      case 'EXIT':
      case 'TIXE':
        DBG("[mgmt] Exit is called. Exiting...");
        MMSG("OKAY");
        xit(0);

      default:
        DBG("[mgmt] Bad data '%s', exiting.", (char*) code);
        MMSG("BAD!");
        xit(1);
    }
  }
}

int main( int argc, char** argv ) {
  struct dtn_args* args;

  file_lockinit();
  memset( &stat_cntr[0], 0, sizeof(stat_cntr) );

  args = args_get( argc, argv );
  VRFY( args, "parsing arguments");

  if (managed) {
    do_management(args);
  } else {

    if (args->disable_io>=11) {
      if (!args->thread_count)
        args->thread_count=1;
      file_iotest( args );
      return 0;
    }

    if (!args->do_server) {
      tx_start( args );
    } else {
      rx_start( args );
    }
  }

  DBG("[MAIN] Reached end of control, exiting.");

  xit(0);

}
