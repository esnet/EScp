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
#include <sys/random.h>

#include <isa-l_crypto.h>

#include "file_io.h"
#include "args.h"

#pragma GCC diagnostic ignored "-Wmultichar"

int      thread_id      __attribute__ ((aligned(64))) = 0;
int      meminit     __attribute__ ((aligned(64))) = 0;
uint64_t tx_filesclosed __attribute__ ((aligned(64))) = 0;

pthread_t DTN_THREAD[THREAD_COUNT];

int did_session_init __attribute ((aligned(64))) = false ;
static struct sess_info sess;

int crash_fd;

struct tx_args {
  struct dtn_args* dtn;
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

  uint8_t buf[2048] __attribute ((aligned(64)));
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

int u64_2flo( uint64_t* target ) {
  int exponent = 64 - __builtin_clzl( *target );

  if (exponent <= 16)
    return 0;

  exponent = exponent - 16;
  *target >>= exponent;

  return exponent;
}

uint64_t flo2_u64( int significand, int exponent ) {
  return (uint64_t) significand << (uint64_t) exponent;
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

int64_t network_recv( struct network_obj* knob, void* aad, uint16_t* subheader ) {

  struct crypto_hdr* chdr = (struct crypto_hdr*) aad;
  struct file_info* fi = (struct file_info*) knob->buf;

  static __thread bool did_init = false;
  int64_t bytes_read=16;

  if (knob->do_crypto) {
    if (chdr->hdr_sz<48) {
      return -1;
    }

    *subheader = chdr->magic;

    knob->iv_incr = chdr->iv;

    aes_gcm_init_128( &knob->gkey, &knob->gctx, knob->iv, aad, 16 );
    VRFY(read_fixed( knob->socket, knob->buf, 16 )>1, );
    bytes_read += 16;

    aes_gcm_dec_128_update(&knob->gkey, &knob->gctx, knob->buf, knob->buf, 16);
  } else {
    memcpy( subheader,  aad, 2 );
    memcpy( knob->buf, aad+2, 14 );
    VRFY(read_fixed( knob->socket, knob->buf+14, 2 ) == 2, );
  }

  if ( !did_init && (*subheader ==  FIHDR_SHORT) ) {
    static struct sess_info s;
    int loop_count=0;

    while (!did_session_init) {
      if ( loop_count++ )
        usleep(1000);
      __sync_fetch_and_add( &did_session_init, 0 );
    }
    memcpy( &s, &sess, sizeof(sess) );
    knob->block = knob->dtn->block  = s.block_sz;
    knob->dtn->thread_count = s.thread_count;

    knob->fob = file_memoryinit( knob->dtn, knob->id );
    (void*) __sync_val_compare_and_swap ( &knob->dtn->fob, 0, (void*) knob->fob );
    __sync_fetch_and_add(&meminit, 1);

    did_init=true;
    DBG("[%2d] Init IO mem ", knob->id );
  }

  if ( *subheader == FIHDR_SHORT ) {
    // Read into buffer from storage I/O
    uint8_t* buffer;

    uint64_t block_sz = flo2_u64( fi->block_sz_significand, fi->block_sz_exponent );

    bytes_read += block_sz;

    VRFY( (knob->token=knob->fob->fetch(knob->fob)) != 0,
          "IO Queue full. XXX: I should wait for it to empty?");

    /* XXX: UIO needs something like this:
    while ( (knob->token=knob->fob->fetch(knob->fob)) == 0 ) {
        // If no buffer is available wait unti I/O engine writes out data
        knob->token = knob->fob->submit(knob->fob, &sz, &res);
        VRFY(sz > 0, "write error");
        knob->fob->complete(knob->fob, knob->token);
    }
    */

    buffer = knob->fob->get( knob->token, FOB_BUF );

    DBV("[%2d] Read of %d sz", knob->id, block_sz);
    if (read_fixed(knob->socket, buffer, block_sz) != block_sz) {
      DBG("[%2d] Returning 0 from network_recv because ... ", knob->id);
      return 0;
    }

    if ( knob->do_crypto ) {
       aes_gcm_dec_128_update( &knob->gkey, &knob->gctx,
         buffer, buffer, block_sz );
    }
  }

  if ( knob->do_crypto ) {
    uint8_t computed_hash[16];
    uint8_t actual_hash[16];

    aes_gcm_dec_128_finalize( &knob->gkey, &knob->gctx, computed_hash, 16 );
    if (read_fixed(knob->socket, actual_hash, 16)<1) {
      NFO("[%2d] Couldn't get auth tag... ", knob->id);
      return 0;
    }
    VRFY( memcmp(computed_hash, actual_hash, 16) == 0,
      "Bad auth tag hdr=%d", *subheader );
    bytes_read += 16;
  }

  return bytes_read;
}


int64_t network_send (
  struct network_obj* knob, void* buf, int sz, int total, bool partial, uint16_t subheader
  ) {

  struct crypto_hdr hdr;
  uint8_t hash[16] = {0};
  uint64_t sent=0, res;
  static __thread bool did_header=0;

  if (knob->do_crypto) {
    if ( !did_header ) {
      hdr.hdr_type = FIHDR_CRYPT;
      hdr.magic = subheader;
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
    if (!did_header) {
      sent =  write_fixed( knob->socket, &subheader, 2 );
      did_header = true;
    }

    sent =  write_fixed( knob->socket, buf, sz );
    if (sent < 1)
      return sent;

    if ( !partial )
      did_header = false;



  }

  return sent;
};


void dtn_waituntilready( void* arg ) {
  struct dtn_args* dtn = arg;
  while ( __sync_fetch_and_add(&dtn->fob, 0) == 0 )
    usleep(10);
}


void* rx_worker( void* arg ) {
  struct rx_args* rx = arg;
  struct dtn_args* dtn = rx->dtn;
  uint32_t id=0, rbuf; // , crc=0;

  uint64_t file_cur=0;

  struct file_object* fob;
  struct file_info* fi;
  struct network_obj* knob=0;

  uint8_t read_buf[16];

  struct file_stat_type  fs;
  struct file_stat_type* fs_ptr=0;
  uint16_t fi_type;

  socklen_t rbuf_sz = sizeof(rbuf) ;

  VRFY( getsockopt(rx->conn, SOL_SOCKET, SO_RCVBUF, &rbuf, &rbuf_sz) != -1,
        "SO_RCVBUF" );
  if ( rbuf != dtn->window ) {
    NFO("rcvbuf sz mismatch %d (cur) != %d (ask)", rbuf, dtn->window);
  }

  id = __sync_fetch_and_add(&dtn->thread_id, 1);

  DBG("[%2d] Accept connection", id);

  affinity_set( dtn );

  while ( 1 ) {
    uint64_t read_sz=read_fixed( rx->conn, read_buf, 16 );
    VRFY (read_sz == 16, "bad read (network), read=%ld", read_sz );

    if ( !knob ) {
      knob = network_initrx ( rx->conn, read_buf, dtn );
      knob->id = id;
      knob->dtn = dtn;

      VRFY(knob, "network init failed");
      if (dtn->do_crypto)
        continue;
    }

    // network_recv will always read into buffer, but buffer is not
    // yet associated with a file descriptor. Read into buffer
    // only occurs if FIHDR_SHORT type is specified.

    if ( (read_sz=network_recv(knob, read_buf, &fi_type)) < 1 ) {
      NFO("[%2d] Bad read=%ld", id, read_sz);
      break;
    }

    fob = knob->fob;

    fi = (struct file_info*) knob->buf;
    /*
    if ( fi_type == FIHDR_CINIT )
      continue;
     */

    if ( fi_type == FIHDR_SESS ) {
      memcpy( &sess, fi, sizeof(sess) );
      __sync_fetch_and_add( &did_session_init, 1 );
      DBG("[%2d] Establish session %016zx bs=%d tc=%d", id,
        sess.session_id, sess.block_sz, sess.thread_count );
      continue;
    }


    if (fi_type != FIHDR_SHORT) {
      VRFY(0, "Unkown header type %d", fi_type);
    }

    { // Do FIHDR_SHORT
      int sz = flo2_u64( fi->block_sz_significand, fi->block_sz_exponent );
      int sz_orig = sz;
      uint64_t file_no = fi->file_no_packed >> 8;
      uint64_t offset = fi->offset & ~(0xffffUL);
      uint64_t res=0;

      fob->set( knob->token, FOB_OFFSET, offset );
      fob->set( knob->token, FOB_SZ, sz );

      if ( sz < 4096 )  {
        // Note: io_flags & O_DIRECT doesn't get set anymore because we always
        //       try to do direct mode... so we always just pad to 4k

        // Always read in at least 4K of data
        fob->set( knob->token, FOB_SZ,  4096 );
        sz_orig=4096;
      }

      while ( file_cur != file_no ) {
        // Fetch the file descriptor associated with block

        VRFY( file_no, "ASSERT: file_no != zero" );

        DBV("[%2d] FIHDR_SHORT: call file_wait for fn=%ld", id, file_no);
        fs_ptr = file_wait( file_no, &fs );

        DBG("[%2d] FIHDR_SHORT: file_wait returned fd=%d for fn=%ld", id, fs.fd, file_no);

        // XXX: VRFY's aren't neccesarily needed here, will fail later anyway.
        VRFY( fs.fd, "[%2d] ASSERT: fd != zero, fn=%ld/%ld", id, fs.file_no, file_no );
        VRFY( fs.file_no == file_no, "[%2d] ASSERT: fs.file_no == file_no, fn=%ld != %ld", id, fs.file_no, file_no );

        file_cur = file_no;
      }

      fob->set( knob->token, FOB_FD, fs.fd );
      fob->flush( fob );

#ifdef O_CRC
      {
        uint8_t* buf = fob->get( knob->token, FOB_BUF );
        crc ^= file_hash( buf, sz, offset/sess.block_sz );
      }
#endif

      DBV("[%2d] Do FIHDR_SHORT crc=%08x fn=%ld offset=%zX sz=%d", id, crc, file_no, offset, sz);

      while ( (knob->token = fob->submit(fob, &sz, &res)) ) {
        // XXX: Flushes IO queue; we don't necessarily want to do that;
        //      For instance when UIO is added back.

        int64_t written;

        /*
        if ( (sz <= 0) && (errno == EBADF) ) {
         NFO("[%2d] repeating write call. fn=%ld. fd=%d", id, file_no, fs.fd);
         usleep(5000000);
         knob->token = fob->submit(fob, &sz, &res);
        }
        */

        VRFY(sz > 0, "[%2d] write error, fd=%d fn=%ld", id, fs.fd, file_no);
        fob->complete(fob, knob->token);
        written = __sync_add_and_fetch( &fs_ptr->bytes_total, sz );
        __sync_fetch_and_add( &dtn->bytes_io, 1 );

        if ( sz != sz_orig ) {
          // XXX: This is probably a fatal error, and should not happen.
          NFO("[%2d] *WARN* Write sz is undersized %d!=%d",
              id, sz, sz_orig);
        }

        DBG("[%2d] FIHDR_SHORT written=%08ld/%08ld fn=%ld os=%zX sz=%d",
            id, written, fs.bytes, file_no, offset, sz );

        if ( fs.bytes && fs.bytes <= written  ) {
          if (fs.bytes != written) {
            fob->truncate(fob, fs.bytes);
            DBG("[%2d] FIHDR_SHORT: close with truncate fn=%ld ", id, file_no);
          } else {
            DBG("[%2d] FIHDR_SHORT: close on fn=%ld ", id, file_no);
          }

          file_incrementtail();
          fob->close(fob);
          memset_avx( fs_ptr );
          __sync_fetch_and_add( &dtn->files_closed, 1 );
        }
      }
    }
  }

  // memcpy( &stat_cntr[id&THREAD_MASK], &local, sizeof(local) );
  if (rx->conn > 1)
    close( rx->conn );

  DBG("[%2d] Return from RX worker thread ... ", id);
  return 0;
}

void* tx_worker( void* args ) {

  struct tx_args* arg = (struct tx_args*) args;

  struct sockaddr_in* saddr ;
  struct dtn_args* dtn = arg->dtn;
  int sock=0, id; // , file_no=-1;
  uint64_t offset;
  int32_t bytes_read;
  struct file_stat_type fs_lcl;
  struct file_stat_type* fs=0;

  int protocol = 0;
  int protocol_sz = sizeof(*saddr);

  uint32_t sbuf;
  struct file_object* fob;

  struct network_obj* knob;
  void* token;

  // uint32_t crc=0;


  affinity_set( dtn );

  id = __sync_fetch_and_add(&thread_id, 1);
  fob = file_memoryinit( dtn, id );
  fob->id = id;

  __sync_fetch_and_add(&meminit, 1);

  (void*) __sync_val_compare_and_swap ( &dtn->fob, 0, (void*) fob );


  DBG( "[%2d] tx_worker: thread start", id);

  // Initialize network
  saddr = (struct sockaddr_in*) &dtn->sock_store[id % dtn->sock_store_count];
  protocol = saddr->sin_family;

  if (protocol == AF_INET6)
    protocol_sz = sizeof(struct sockaddr_in6);
  else
    VRFY( protocol == AF_INET, "INET family %d not expected connection %d",
          protocol, id % dtn->sock_store_count );

  sbuf = dtn->window;
  socklen_t sbuf_sz = sizeof(sbuf);

  VRFY((sock = socket( protocol, SOCK_STREAM, 0)) != -1, "Parsing IP");
  VRFY(setsockopt(sock, IPPROTO_TCP, TCP_MAXSEG,
                  &dtn->mtu, sizeof(dtn->mtu)) != -1,);
  VRFY(setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sbuf, sbuf_sz) != -1,);
  VRFY(getsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sbuf, &sbuf_sz) != -1,);

  if (dtn->pacing) {
    VRFY(setsockopt(sock, SOL_SOCKET, SO_MAX_PACING_RATE, &dtn->pacing, sizeof(dtn->pacing)) != -1,);
  }

  if (sbuf != dtn->window) {
    NFO("[%d] Requested TCP window size of %d, but got %d bytes",
        id, dtn->window, sbuf );
  }

  VRFY( -1 !=
    connect(sock, (void *)saddr, protocol_sz),
    "Connecting to remote host" );

  VRFY( getsockopt( sock, IPPROTO_TCP, TCP_MAXSEG, &sbuf, &sbuf_sz) != -1, );

  if ( (sbuf != dtn->mtu) && ((sbuf+12) != dtn->mtu) ) {
    NFO("[%d] TCP_MAXSEG value is %d, requested %d", id, sbuf, dtn->mtu);
  }

  knob = network_inittx( sock, dtn );
  VRFY (knob != NULL, );

  // Finish netork init
  DBG("[%2d] tx_worker: connected and ready", id);

  if (!id) {
    // Worker ID==0; Send "session begin" message
    // FUTURE: Remove this; handle at RUST layer
    struct sess_info s;

    s.hdr_type=FIHDR_SESS;
    s.block_sz = dtn->block;
    s.thread_count = dtn->thread_count;
    s.session_id = dtn->session_id;

    DBG("[%2d] Set session %016zx bs=%d tc=%d", id,
      s.session_id, s.block_sz, s.thread_count);
    VRFY( network_send( knob, &s, 16, 16, false, FIHDR_SESS ) > 0, "" );
  }


  // Start TX transfer session
  while (1) {

    if (!fs) {
      fs = file_next( id, &fs_lcl );

      if ( !fs ) {
        DBG("[%2d] Finished reading file(s), exiting...", id );
        break;
      }

      // file_no = fs->file_no;
      // crc= 0;
    }

    if (!fs_lcl.fd) {
      DBG("[%2d] tx_worker exiting because fd provided is zero", id );
      break;
    }

    while ( (token=fob->fetch(fob)) ) {
      // We get as many I/O blocks as we can, and populate them
      // with operations. The assumption is that the file is large
      // and we will be able to read all of these. With small files
      // the extra I/O operations are superflus.

      offset = __sync_fetch_and_add( &fs->block_offset, 1 );
      offset *= dtn->block;

      DBG("[%2d] FIHDR offset: fn=%ld offset=%lX state=%lX", id, fs_lcl.file_no, offset, fs_lcl.state);

      fob->set(token, FOB_OFFSET, offset);
      fob->set(token, FOB_SZ, dtn->block);
      fob->set(token, FOB_FD, fs_lcl.fd);
    }

    token = fob->submit( fob, &bytes_read, &offset );
    if (!token) {
      NFO("[%2d] fob->submit resulted in an emptry result fn=%ld", id, fs_lcl.file_no);
      continue;
    }

    if (bytes_read <= 0) {
      if (bytes_read == 0 /* EOF */ ) {

        int wipe = 0;

        if (file_iow_remove( fs, id ) == (1UL << 30)) {
          fob->close( fob );
          int64_t res = __sync_add_and_fetch( &tx_filesclosed, 1 );
          DBG("[%2d] Worker finished with fn=%ld files_closed=%ld; closing fd=%d",
              id, fs_lcl.file_no, res, fs_lcl.fd);
          wipe ++;
        } else {
          DBG("[%2d] Worker finished with fn=%ld", id, fs_lcl.file_no);
        }

        while ( (token=fob->submit( fob, &bytes_read, &offset )) ) {
          // Drain I/O queue of stale requests
          fob->complete(fob, token);
        };

        if (wipe) {
          DBV("[%2d] Wiping fn=%ld", id, fs_lcl.file_no);
          memset_avx((void*) fs);
        }

        fs = 0;
        continue;
      }

      VRFY( bytes_read >= 0, "[%2d] Read Error fd=%d fn=%ld offset=%ld %lX/%lX", id, fs_lcl.fd, fs_lcl.file_no, offset, fs_lcl.state, fs->state );
      return (void*) -1; // Not reached
    }

    {
      uint8_t* buf = fob->get( token, FOB_BUF );
      struct file_info fi = {0};
      int bytes_sent;

      uint64_t significand = bytes_read;
      int exponent = u64_2flo(&significand);

      fi.file_no_packed = fs_lcl.file_no << 8ULL;
      fi.block_sz_exponent = exponent;

      fi.offset = offset;
      fi.block_sz_significand = significand;

      bytes_sent = flo2_u64( significand, exponent );

#ifdef O_CRC
      crc ^= file_hash( buf, bytes_sent, offset );
#endif

      DBG("[%2d] FI_HDR sent with fn=%ld offset=%lX, bytes_read=%d, bytes_sent=%d",
          id, fs_lcl.file_no, offset, bytes_read, bytes_sent);

      VRFY(fi.file_no_packed >> 8, "[%2d] ASSERT: file_no != 0, fn=%ld", id, fs_lcl.file_no);

      VRFY( network_send(knob, &fi, 16, 16+bytes_sent, true, FIHDR_SHORT) > 0, );
      VRFY( network_send(knob, buf, bytes_sent, 16+bytes_sent, false, FIHDR_SHORT) > 0, );

      __sync_fetch_and_add( &fs->bytes_total, bytes_read );
      __sync_fetch_and_add( &dtn->bytes_io, bytes_read );

      fob->complete(fob, token);

      DBV("[%2d] Finish block crc=%08x fn=%d offset=%zx sz=%d sent=%d",
          id, crc, file_no, offset, bytes_read, bytes_sent);
    }

  }

  /*
  memcpy( &stat_cntr[id&THREAD_MASK], &local, sizeof(local) );
  */

  DBG("[%2d] Thread exited", id );
  return 0;
}

void finish_transfer( struct dtn_args* args, uint64_t filecount ) {

  // Typically this function is called with filecount argument, and will wait
  // until all files written, This way the receiver can verify to the sender
  // that all files were transferred successfully. Conversely it should return
  // an error on failure.

  DBG("[--] finish_transfer is called fc=%ld", filecount);

  int j = 0;

  while (filecount) {
    uint64_t files_closed = __sync_fetch_and_add( &args->files_closed, 0 );
    j+=1;

    if (files_closed >= filecount) {
      DBG("[--] finish_transfer complete");
      return;
    }

    if ((j & 0x3ff ) == 0x3ff) {
      DBG("[--] Waiting on %ld/%ld", files_closed, filecount);
    }

    usleep(1000);
  }

  for (int i=0; i < args->thread_count; i++)
    pthread_join( DTN_THREAD[i], NULL );
}

void tx_start(struct dtn_args* args ) {
  int i=0;

  pthread_attr_t attr;
  pthread_attr_init(&attr);

  static struct tx_args tx_arg[THREAD_COUNT] = {0};
DBG("tx_start spawning workers");
  if (!args->thread_count)
    args->thread_count=1;

  for (i=0; i < args->thread_count; i++)  {
    char buf[16];
    tx_arg[i].dtn = args;

    VRFY(  0 == pthread_create(
          &DTN_THREAD[i], &attr, tx_worker, (void*) &tx_arg[i] ),
          "tx_start: Error spawining tx_worker" );

    sprintf(buf, "TX_%d", i);
    pthread_setname_np( DTN_THREAD[i], buf);
  }

  while ( __sync_fetch_and_add(&meminit, 0 ) != args->thread_count )
    usleep(10);

  DBG("tx_start workers finished initializing structures");
  return;
}

uint64_t tx_getclosed() {
  return __sync_fetch_and_add( &tx_filesclosed, 0 );
}

int rx_start( void* fn_arg ) {
  int i=0,j=0, sock;
  struct dtn_args* args = fn_arg;
  struct sockaddr_in*  saddr = (void*) &args->sock_store[0];
  struct sockaddr_in6* saddr6 = (void*) &args->sock_store[0];
  int addr_sz, port;
  static struct rx_args rx_arg[THREAD_COUNT] = {0};

  // XXX: Syntactically and programatically we support multiple interfaces
  //      but we don't implement a listener on anything but the fist iface
  //      because at some point this code changed and support for multiple
  //      interfaces was never added back.

  DBG("Start: rx_start");

  args->thread_count=0;

  port = ntohs(saddr->sin_port);
  if ( saddr->sin_family == AF_INET )
    addr_sz = sizeof(struct sockaddr_in);
  else
    addr_sz = sizeof(struct sockaddr_in6);

  VRFY ( (sock = socket( saddr->sin_family, SOCK_STREAM, 0)) != -1, );

  while (bind(sock, (struct sockaddr*) saddr, addr_sz) == -1) {
    // Keep trying to bind to a port until we get to one that is open

    if ( ++j > 100 ) {
      ERR ( "binding to port(s) %d-%d", port-j+1, port );
      return -1;
    }

    close(sock);
    saddr->sin_port = htons( ++port );
    VRFY ( (sock = socket( saddr->sin_family, SOCK_STREAM, 0)) != -1, );

  }

  VRFY ( listen( sock, THREAD_COUNT ) != -1, "listening" );

  /* setsockopt(sock, SOL_SOCKET, SO_RCVLOWAT, &Block_sz, sizeof(Block_sz)) */

  {
    uint64_t rbuf = args->window;
    socklen_t rbuf_sz = sizeof(rbuf);
    if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rbuf, rbuf_sz) == -1) {
      perror("SO_RCVBUF");
      exit(-1);
    }
  }

  {
    char buf[500];
    if (saddr->sin_family == AF_INET) {
      inet_ntop( saddr->sin_family, &saddr->sin_addr, buf, addr_sz );
    } else {
      inet_ntop( saddr->sin_family, &saddr6->sin6_addr, buf, addr_sz );
    }

    NFO("Listening on [%s]:%d", buf, ntohs(saddr->sin_port));
  }

  {
    uint16_t old_port = args->active_port;
    uint16_t new_port = ntohs(saddr->sin_port);
    uint16_t res;

    res = __sync_val_compare_and_swap(&args->active_port, old_port, new_port);
    VRFY( res == old_port, "Bad val in args->active_port");
  }

  while(1) {
    char buf[16];

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    socklen_t saddr_sz = sizeof(struct sockaddr_in);

    fcntl( sock, F_SETFL, O_NONBLOCK );
    while (1) {
      rx_arg[i].conn = accept( sock, (struct sockaddr *) saddr, &saddr_sz );
      rx_arg[i].dtn = args;
      if (rx_arg[i].conn > 0)
        break;
      if (args->thread_count && (i >= args->thread_count)) {
        DBG("Finished spawning workers");
        return 0;
      }
      usleep(1000);
    }

    if (  pthread_create(
            &DTN_THREAD[i], &attr, rx_worker, (void*) &rx_arg[i] )
       ) {
      perror("pthread_create");
      close(rx_arg[i].conn);
      continue;
    }

    sprintf(buf, "RX_%d", i);
    pthread_setname_np( DTN_THREAD[i], buf);

    i = (i+1) % THREAD_COUNT;
  }

  return 0;
}

char decode_bool( bool b ) {
  return b? 'Y': 'N';
}

void print_args ( struct dtn_args* args ) {

  printf(" Receiver=%c, SSH=%c, Crypto=%c, Hash=%c, Affinity=%c disable_io=%c\n",
    decode_bool( args->do_server ),
    decode_bool( args->do_ssh ),
    decode_bool( args->do_crypto ),
    decode_bool( args->do_hash ),
    decode_bool( args->do_affinity ),
    decode_bool( args->disable_io )
  );


  printf(" IO Flags: O_WRONLY=%c, O_DIRECT=%c, O_CREAT=%c, O_TRUNC=%c \n",
    decode_bool( args->flags & O_WRONLY ),
    decode_bool( args->flags & O_DIRECT ),
    decode_bool( args->flags & O_CREAT  ),
    decode_bool( args->flags & O_TRUNC  )
  );

  /*
  printf(" Block Size: %s, QD: %d, IO_Engine: %s/%d, window: %s, thread_count: %d\n",
    human_write( args->block, true ), args->QD, args->io_engine_name,
    args->io_engine, human_write(args->window, true), args->thread_count
  );
  */


};

int64_t get_bytes_io( struct dtn_args* dtn ) {
  return __sync_fetch_and_add( &dtn->bytes_io, 0 );
}

void tx_init( struct dtn_args* args ) {
  if (args->do_crypto) {
    int res = getrandom(args->crypto_key, 16, GRND_RANDOM);
    // XXX: We should handle the case of getting less than
    //      16 bytes of data.
    VRFY( res == 16, "Error initializing random key");
  }
}


