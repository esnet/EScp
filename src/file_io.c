#define _GNU_SOURCE

#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/mman.h>

#include "file_io.h"
#include "args.h"
#include "es_shm.h"

pthread_mutex_t file_next_lock;
pthread_cond_t file_next_cv;
pthread_cond_t file_comp_cv; // Signals when files complete

struct file_stat_type file_stat[THREAD_COUNT*2]={0};

uint64_t rand_seed=0;      // for Dummy IO
uint64_t bytes_written=0;  // for Dummy IO

bool file_persist_state = false;

struct file_name_type file_list[FILE_LIST_SZ] = {0};

uint64_t file_count = 0;
uint64_t file_completed = 0;
uint64_t file_wanted = 0;


void file_lockinit() {
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

  pthread_mutex_init( &file_next_lock, &attr );
  pthread_cond_init( &file_next_cv, NULL );
  pthread_cond_init( &file_comp_cv, NULL );
}

struct file_name_type* file_nameget(uint64_t file_no) {
  // Must already obtain thread lock

  while ( file_list[file_no % FILE_LIST_SZ].state & 2 ) {
    DBV("Got state=2, will wait for file");
    pthread_cond_wait( &file_next_cv, &file_next_lock );
  }

  return &file_list[file_no % FILE_LIST_SZ];

  // Returns &file_name_type where state in [0,1]
}

void file_compute_totals( int start ) {
  // We return the bytes total starting from start file but always
  // return the total number of successful file_stats

  int count=0;
  int i=0;
  uint64_t total=0;
  int file_stated=0;

  VRFY( pthread_mutex_lock( &file_next_lock ) == 0, );
  count = file_count;
  pthread_mutex_unlock( &file_next_lock );

  for (i=start; i<count; i++) {
    struct stat st;

    if ( stat( file_list[i % FILE_LIST_SZ].name, &st ) )
      continue;

    file_stated++;
    total+=st.st_size;
  }

  MMSG("FTOT\n%d %zd", file_stated, total);

}


void file_persist( bool state ) {
  VRFY( pthread_mutex_lock( &file_next_lock ) == 0, );

  file_persist_state = state;

  VRFY( pthread_cond_broadcast( &file_next_cv ) == 0, );
  pthread_mutex_unlock( &file_next_lock );

}

static inline uint64_t xorshift64s(uint64_t* x)
{
  *x ^= *x >> 12; // a
  *x ^= *x << 25; // b
  *x ^= *x >> 27; // c
  return *x * 0x2545F4914F6CDD1DUL;
}


struct file_stat_type* file_wait( struct file_object* fob, uint32_t file_no ) {
  struct file_stat_type* ret = NULL;
  int i;

  VRFY( pthread_mutex_lock( &file_next_lock ) == 0, );
  while (!ret) {
    for (i=0; i < THREAD_COUNT*2; i++) {
      if ( file_stat[i].file_no == file_no ) {
        ret = &file_stat[i];
        break;
      }
    }

    if (!ret)
      pthread_cond_wait( &file_next_cv, &file_next_lock );
  }

  ret->worker_map |= 1LL << fob->id;
  pthread_mutex_unlock( &file_next_lock );

  return ret;
}

int32_t file_hash( void* block, int sz, int seed ) {
  uint32_t *block_ptr = (uint32_t*) block;
  uint64_t hash = seed;
  hash = xorshift64s(&hash);
  int i=0;

  if (sz%4) {
    for ( i=0; i < (4-(sz%4)); i++ ) {
      ((uint8_t*)block_ptr)[sz+i] = 0;
    }
  }

  for( i=0; i<(sz+3)/4; i++ ) {
    hash = __builtin_ia32_crc32si( block_ptr[i], hash );
  }

  return hash;
}

static inline int file_add_raw( void* file_name, int64_t no ) {

  if (no)
    no -= 1;
  else
    no = file_count;

  no = no % FILE_LIST_SZ;
  if ( !file_persist_state && file_list[no].state ) {
    DBV("file_add: file '%s' exceeds static file limits", (char*)file_name );
    return -1;
  }

  while ( file_list[no].state != 0 ) {
    DBV("file_add: Waiting because state != 0. no=%zd fn=%s", no,
      file_list[no].name );
    pthread_cond_wait( &file_next_cv, &file_next_lock );
  }

  file_list[no].name = malloc(strlen(file_name)+8);
  VRFY( file_list[no].name, "bad malloc" );

  file_list[no].state = 1;
  strcpy( file_list[no].name, file_name );
  file_list[no].fino = file_count;

  file_count ++;
  VRFY( pthread_cond_broadcast( &file_next_cv ) == 0, );

  return 0;
}

int file_add( void* file_name, uint64_t file_no ) {
  int ret;

  DBV("file_add: add file '%s'/%zd", (char*)file_name, file_no );

  VRFY( pthread_mutex_lock( &file_next_lock ) == 0, );
  ret = file_add_raw( file_name, file_no );
  pthread_mutex_unlock( &file_next_lock );

  DBV("file_add: finished adding file.... ");

  return ret;
}

void file_add_bulk( void* arg, uint32_t count ) {
  int i=0;
  char** files = (char**) arg;

  VRFY( pthread_mutex_lock( &file_next_lock ) == 0, );
  for (i=0; i<count; i++) {
    file_add_raw( files[i], 0 );
  }
  pthread_mutex_unlock( &file_next_lock );

}

struct file_stat_type* file_next( struct file_object* fob,
                                  int file_no, void* file_name ) {

  // Generic function to fetch the next file, may return FD to multiple
  // threads depending on work load / incomming file stream.
  //

  static uint64_t file_stat_count=0;
  static __thread struct file_stat_type* fs= &file_stat[0];
  int i;
  bool found = false;
  struct file_name_type* curfile;


  if (file_name) {
    DBV("[%2d] Entered file_next with file_no=%d name=%s",
      fob->id, file_no, (char*) file_name);
  }
  else  {
    DBV("[%2d] Entered file_next with file_no=%d", fob->id, file_no );
  }

  VRFY( pthread_mutex_lock( &file_next_lock )==0, "pthread_lock" );

  if (file_no) {
    curfile = file_nameget(file_no-1);
    if (file_name)
      // after potential wait on slot, add filename and update state
      file_add_raw( file_name, file_no );
  } else
    curfile = file_nameget(file_stat_count);

  if ( !curfile->state ) {
    // No new files

    if ( file_no ) {
      // This is a receiver error, because filename should either be
      // set through management or from FIHDR_LONG message.
      pthread_mutex_unlock( &file_next_lock );

      NFO("file_no=%d does not have a corresponding filename", file_no);
      DBV("[%2d] No work for thread (RX)", fob->id);

      return 0;
    } else {
      // We are sender, try to attach to existing session

      while ( !found ) {
        DBV("[%2d] Thread looking for work", fob->id);
        for (i=0; i<THREAD_COUNT*2; i++) {
          fs = &file_stat[i];

          if ( (fs->pending || fs->worker_map) && !fs->flushing ) {
            found = true;
            break;
          }
        }

        if ( !found && file_persist_state ) {
          DBV("[%2d] file__next: wait=true (RX), waiting for file", fob->id);
          pthread_cond_wait( &file_next_cv, &file_next_lock );

          // Check if a new file has been added
          curfile = file_nameget(file_stat_count);
          if (curfile->state)
            break;

          continue;
        }

        if ( !found ) {
          pthread_mutex_unlock( &file_next_lock );

          DBV("[%2d] No work for thread (TX)", fob->id);
          return 0;
        }

        fs->pending |= 1LL << fob->id;
        fob->fd = fs->fd;

        pthread_mutex_unlock( &file_next_lock );
        DBV("[%2d] Adding worker to '%s'", fob->id, fs->name);
        return fs;
      }
    }
  }

  while ( !found ) {
    for (i=0; i<THREAD_COUNT*2; i++) {
      fs = &file_stat[i];
      if ( !fs->worker_map  && !fs->pending ) {
        found = true;
        break;
      }
    }

    if ( !found ) 
      pthread_cond_wait( &file_next_cv, &file_next_lock );
  }

  memset( fs, 0, sizeof( struct file_stat_type ) );

  if (file_no)
    fs->worker_map |= 1LL << fob->id;
  else
    fs->pending |= 1LL << fob->id;

  curfile->state |= 2;
  if ( fob->io_flags & O_WRONLY )
    fob->fd = fob->open( curfile->name,  fob->io_flags, 0644 );
  else
    fob->fd = fob->open( curfile->name,  fob->io_flags, 00 );

  VRFY( fob->fd > 0, "[%2d] opening '%s'; flags=%x, errno=%s",
                      fob->id, curfile->name, fob->io_flags, strerror(errno) );

  fs->fd = fob->fd;
  fs->name = curfile->name;

  file_stat_count ++;

  if (file_no)
    fs->file_no = file_no;
  else
    fs->file_no = file_stat_count;


  VRFY( pthread_cond_broadcast( &file_next_cv ) == 0, );
  VRFY( pthread_mutex_unlock(&file_next_lock) == 0, "" );

  DBV("[%2d] Opened file %s, fd=%d, flags=%x fn=%d",
    fob->id, curfile->name, fob->fd, fob->io_flags, fs->file_no );
  MMSG("OPEN\n%d %s", fs->file_no, fs->name);

  return fs;
}

void file_mark_worker( struct file_stat_type* fs, int id) {
  VRFY( pthread_mutex_lock(&file_next_lock) == 0, "" );
  DBV("[%2d] mark_worker changed pending state", id);

  fs->pending &= ~(1LL << id);
  fs->worker_map |= 1LL << id;

  pthread_cond_broadcast( &file_next_cv );
  VRFY( pthread_mutex_unlock(&file_next_lock) == 0, "" );

}



void* file_close( struct file_object* fob, struct file_stat_type* fs,
  uint64_t bytes_total, uint32_t crc, bool abbreviated, uint32_t given_crc,
  int given_workers ) {

  int did_close=0;
  int workers=given_workers;
  char buf[2048];

  static __thread struct file_close_struct ret;

  DBV("[%2d] file_close: Entering file_close\n", fob->id);
  VRFY( fs != 0, "Double close on file stat object" ); // should never happen

  VRFY( pthread_mutex_lock(&file_next_lock) == 0, "" );

  if (given_workers)
    fs->pending=0;

  fs->pending &= ~(1LL << fob->id);

  if (abbreviated) {
    fs->flushing++;
    pthread_cond_broadcast( &file_next_cv );
    pthread_mutex_unlock(&file_next_lock);
    DBV("[%2d] file_close: removed pending flag\n", fob->id);
    return 0;
  }

  if (given_crc) {
    DBV("[%2d] file_close: set crc=%08x\n", fob->id, given_crc);
    fs->given_crc = given_crc;
  }

  while ( fs->pending & ~(1LL << fob->id)  ) {
    if (!fs->worker_map) {
      // Everyone is holding a pending, safe to exit
      DBV("[%2d] file_close: not waiting on pending, no workers\n", fob->id);
      break;
    }
    DBV("[%2d] file_close: wait on pending %04x %s",
      fob->id, fs->pending, fs->name);
    pthread_cond_wait( &file_next_cv, &file_next_lock );
  }

  if (fs->pending & (1LL << fob->id) )
    pthread_cond_broadcast( &file_next_cv );

  fs->pending &= ~(1LL << fob->id);

  if ( !(fs->worker_map & (1LL << fob->id)) ) {
    NFO("[%2d] file_close: close on '%s' without open",
      fob->id, fs->name);
    VRFY( pthread_mutex_unlock(&file_next_lock) == 0, "" );
    return 0;
  }

  {
    // This is how the sender computes it's worker count
    workers = __builtin_popcountl( fs -> worker_map );

    if (fs->workers < workers)
      fs->workers = workers;
  }

  fs->worker_map &= ~(1LL << fob->id);

  ++fs->flushing;
  fs->bytes_total += bytes_total;
  fs->crc ^= crc;

  ret.sz = fs->bytes_total;
  ret.file_no = fs->file_no;
  ret.flushing = fs->flushing;
  ret.given_crc = fs->given_crc;

  if ( strlen(fs->name)  > 75 ) {
    strncpy( ret.fn, fs->name, 70 );
    strncpy( ret.fn+70, "...", 4 );
  } else {
    strncpy( ret.fn, fs->name, 76 );
  }
  ret.workers = fs->workers;
  ret.hash = fs->crc;

  if ( fs -> worker_map || (fs->flushing < fs->workers)
                        || (fs->flushing < given_workers) ) {
    if (given_workers)
      fs->pending=1;
    DBV("[%2d] releasing interest in '%s', %d workers left",
      fob->id, fs->name, __builtin_popcountl( fs -> worker_map ));
  } else {
    int fno = (fs->file_no-1) % FILE_LIST_SZ;
    did_close=1;
    snprintf( buf, 2000, "STOP\n%d %s", fs->file_no, fs->name );
    DBV("[%2d] Calling close on '%s'", fob->id, fs->name );

    if ( file_list[fno].state ) {
      free( file_list[fno].name );
      file_list[fno].name = 0;
      file_list[fno].state = 0;
      file_list[fno].fino= 0;
    }

    fob->close( fs -> fd );
    VRFY( pthread_cond_broadcast( &file_next_cv ) == 0, );
    file_completed ++;
  }

  ret.did_close = did_close;
  VRFY( pthread_mutex_unlock(&file_next_lock) == 0, "" );

  if (did_close) {
    MMSG("%s", buf);
  }

  return &ret;
}

struct file_object* file_memoryinit( void* arg, int id ) {
  struct dtn_args* dtn = arg;
  static __thread struct file_object fob = {0} ;

  fob.io_type = dtn->io_engine;
  fob.QD = dtn->QD;
  fob.blk_sz = dtn->block;
  fob.io_flags = dtn->flags;
  fob.thread_count = dtn->thread_count;
  fob.args = &dtn->io_engine_name[5];
  fob.id = id;

  switch (fob.io_type) {
#ifdef __ENGINE_POSIX__
    case FIIO_POSIX:
      file_posixinit( &fob );
      break;
#endif
#ifdef __ENGINE_URING__
    case FIIO_URING:
      file_uringinit( &fob );
      break;
#endif
#ifdef __ENGINE_DUMMY__
    case FIIO_DUMMY:
      file_dummyinit( &fob );
      break;
#endif
#ifdef __ENGINE_URING__
    case FIIO_SHMEM:
      shmem_init( &fob );
      break;
#endif
    default:
      VRFY( 0, "No matching engine for '%x'",
               fob.io_type );
  }

  return &fob;

}

uint8_t b64_enc[] = {
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N',
  'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b',
  'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p',
  'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '0', '1', '2', '3',
  '4', '5', '6', '7', '8', '9', '+', '/' };

uint8_t b64_dec[256] = {
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0, 62,  0,  0,  0, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,  0,  0,
  0,  0,  0,  0,  0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,  0,  0,  0,  0,  0,  0, 26, 27, 28,
 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
 49, 50, 51 };

int file_b64encode( void* dst, void* src, int len ) {
  uint8_t* d = (uint8_t*) dst;
  uint8_t* s = (uint8_t*) src;
  int i=0,j=0;

  while ( j + 2 < len ) {
    d[i++] = b64_enc[s[j] >> 2];
    d[i++] = b64_enc[((s[j] & 0x3)   << 4) + (( s[j+1] & 0xf0 ) >> 4)];
    d[i++] = b64_enc[((s[j+1] & 0xf) << 2) + (s[j+2]  >> 6)];
    d[i++] = b64_enc[s[j+2] & 0x3f];
    j+=3;
  }

  if ( j<len ) {
    d[i++] = b64_enc[s[j] >> 2];
    d[i] = b64_enc[((s[j] & 0x3) << 4)];
    if ( (j + 1) >= len  )
      return i+1;
    d[i++] = b64_enc[((s[j] & 0x3)   << 4) + (( s[j+1] & 0xf0 ) >> 4)];
    d[i++] = b64_enc[((s[j+1] & 0xf) << 2)] ;
  }

  return i;
}

int file_b64decode( void* dst, void* src, int len ) {
  uint8_t* r = (uint8_t*) dst;
  uint8_t* s = (uint8_t*) src;
  int i=0,j=0;

  while ( (j+3) < len ) {
    int a= b64_dec[ s[j] ],
        b= b64_dec[ s[j+1] ],
        c= b64_dec[ s[j+2] ],
        d= b64_dec[ s[j+3] ];

    r[i]   = (a << 2) | (b >> 4);
    r[i+1] = (b << 4) | (c >> 2);
    r[i+2] = (c << 6) | d;
    i+=3;
    j+=4;
  }

  if ( j < len ) {
    int a= b64_dec[ s[j] ],
        b= b64_dec[ s[j+1] ],
        c= b64_dec[ s[j+2] ];

    r[i]   = (a << 2);
    if ( (j+1) >= len  )
      return i+1;

    r[i]   = (a << 2) | (b >> 4);
    r[i+1] = (b << 4) ;

    if ( (j+2) >= len  )
      return i+2;

    r[i+1] = (b << 4) | (c >> 2);
    r[i+2] = (c << 6);
    return i+3;
  }

  return i;
}

void file_prng( void* buf, int sz ) {
  int i=0, offset=0;
  uint64_t* b = buf;
  static __thread uint64_t s=0;

  if ( s == 0 )
    file_randrd( &s, 8 );

  while ( sz > 1023 ) {
    // Compiler optimization hint
    for (i=0; i<128; i++)
      b[offset+i] = xorshift64s(&s);
    offset += 128;
    sz -= 1024;
  }

  for ( i=0; i < ((sz+7)/8); i++ ) {
      b[offset+i] = xorshift64s(&s);
  }
}


void* file_ioworker( void* arg ) {
  struct file_stat_type* file_stat=0;
  struct dtn_args* dtn = arg;
  struct file_object* fob;
  int id = __sync_fetch_and_add(&dtn->thread_id, 1);
  uint64_t s = rand_seed;

  uint8_t* buffer;
  uint64_t res;
  void* token;
  int32_t sz;
  int did_work;
  uint32_t crc=0;

  affinity_set( dtn );
  fob = file_memoryinit( dtn, id );
  dtn->fob = fob;

  if (s) {
    // Seed S differently for each thread
    s ^= 0x94D049BB133111EBUL * id;
  }

  fob->id = id;
  DBV("[%2d] Start worker", id);

  while (1) {
    uint64_t offset;
    int64_t io_sz=1;

    if (!file_stat) {

      file_stat = file_next( fob, 0, 0 );
      if (!file_stat) {
        DBV("[%2d] no more work, exit work loop", id);
        break;
      } else {
        DBV("[%2d] got work", id);
      }
      crc= did_work= 0;

    }

    while ( (token=fob->fetch(fob)) ) {

      offset = __sync_fetch_and_add( &file_stat->block_offset, 1 );
      offset *= dtn->block;
      fob->set(token, FOB_OFFSET, offset);

      if ( dtn->flags & O_WRONLY ) {
        io_sz = dtn->io_bytes - offset;

        if ( io_sz > dtn->block )
          io_sz = dtn->block;
      } else {
        io_sz = dtn->block;
      }

      if (io_sz <= 0) {
        fob->set( token, FOB_SZ, 0 );
        break;
      }

      fob->set( token, FOB_SZ, io_sz );

      buffer = fob->get(token, FOB_BUF );

      if (s) {
        int i,j;
        for (i=0; i<dtn->block; i+=4096)
          for (j=0; j<4096; j+=8)
            ((uint64_t*)(buffer+i+j))[0] = xorshift64s(&s);
      }

    }

    sz = dtn->block;
    if ( (token=fob->submit(fob, &sz, &res)) ) {
      VRFY ( sz >= 0, "IO Error");

      if (sz<1) {
        struct file_close_struct* fc;

        XLOG("file_ioworker: queue_flush");

        while ( (token = fob->submit(fob, &sz, &res)) ) {
          fob->complete(fob, token);
        }

        XLOG("file_ioworker: flush complete");

        fc = file_close( fob, file_stat, 0, crc, 0, 0, 0 );
        if (fc->did_close)
          NFO("'%s': %08X", fc->fn, fc->hash);
        file_stat = 0;
        continue;
      }

      if (!did_work) {
        did_work=1;
        file_mark_worker( file_stat, id );
      }

      offset = (uint64_t) fob->get( token, FOB_OFFSET );
      buffer = fob->get( token, FOB_BUF );

      crc ^= file_hash( buffer, sz, (offset+dtn->block-1LL)/dtn->block );
      fob->complete(fob, token);

      __sync_fetch_and_add( &bytes_written, sz );
    }
  }

  while ( (token=fob->submit(fob, &sz, &res)) ) {
    fob->complete(fob, token);
    VRFY ( sz >= 0 , "IO Error");
    __sync_fetch_and_add( &bytes_written, sz );
  }

  DBV("[%2d] worker finished", id);
  return 0;
}

void file_randrd( void* buf, int count ) {
    int fd = open("/dev/urandom", O_RDONLY);
    VRFY( fd > 0, );
    VRFY( count == read(fd, buf, count), );
    close(fd);
}

void file_iotest( void* args ) {
  int i=0;
  struct dtn_args* dtn = args;
  pthread_t thread[THREAD_COUNT];
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  char* op = "Read";

  struct timeval t;
  uint64_t start_time, now_time;
  float duration;

  if (dtn->disable_io > 11) {
    file_randrd( &rand_seed, 8 );
  }

  gettimeofday( &t, NULL );
  start_time = t.tv_sec * 1000000 + t.tv_usec;

  if ( dtn->flags & O_WRONLY )
    op = "Written";

  for ( i=0; i< dtn->thread_count; i++ ) {
    if ( pthread_create( &thread[i], &attr, &file_ioworker, dtn ) ) {
      perror("pthread");
      exit(-1);
    }
  }

  for ( i=0; i< dtn->thread_count; i++ )
    pthread_join( thread[i], NULL );

  gettimeofday( &t, NULL );
  now_time = t.tv_sec * 1000000 + t.tv_usec ;
  duration = (now_time - start_time) / 1000000.0;

  NFO(
      "%s=%siB, Elapsed= %.2fs, %sbit/s %siB/s",
      op, human_write(bytes_written, true),
      duration, human_write(bytes_written/duration,false),
      human_write(bytes_written/duration,true)
  );

  MMSG("Bytes %s: %sbit/s %s",
       op,
       human_write(bytes_written/duration,false),
       human_write(bytes_written,true)  );


}
