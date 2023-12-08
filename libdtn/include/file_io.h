#include <stdbool.h>
#include <stdint.h>
#include <sys/uio.h>
#include <dirent.h>


#ifndef __FILE_IO_DOT_H__
#define __FILE_IO_DOT_H__

#define FIHDR_SESS  1
#define FIHDR_SHORT 2
#define FIHDR_LONG  3
#define FIHDR_END   4
#define FIHDR_CINIT 5
#define FIHDR_CRYPT 6

#define FIEND_FILE 1
#define FIEND_SESS 2

#define FIIO_POSIX 1
#define FIIO_URING 2
#define FIIO_DUMMY 3
#define FIIO_SHMEM 4

#define FOB_SZ 1
#define FOB_OFFSET 2
#define FOB_BUF 3
#define FOB_FD 4
#define FOB_TRUNCATE 5

struct posix_op {
  union {
    struct {
      uint8_t* buf;
      uint64_t sz;
    };
    struct iovec vec;
  }; // 16
  uint64_t offset;
  uint32_t flags;
  uint32_t fd; // 32
};

struct file_object {
  // Generic object that abstracts file_io operation to different engines
  int32_t QD;
  int32_t pad12;

  uint32_t blk_sz;
  uint16_t id;
  uint16_t io_type;  // Posix, uring, ...

  int64_t  head;
  int64_t  tail;     // 32

  uint64_t pad11;    //
  int32_t  io_flags; // i.e. O_DIRECT, O_RDONLY
  int32_t  io_ret;   // 48

  uint32_t thread_count;
  uint64_t pad1;
  uint32_t pad2;      // 64


  void*    pvdr;     // Provider internal ptr
  char*    args;

  int   (*open)    (const char*, int, ...);

  DIR*   (*fopendir) (int);
  struct dirent* (*readdir) (DIR *dirp);
  int   (*close_fd) (int);

  int   (*close)    (void*);
  int   (*truncate) (void*, int64_t);
  int   (*fstat)   (int, struct stat*);

  void* (*fetch)   (void*);
  void  (*flush)   (void*);
  void* (*set)     (void*, int32_t key, uint64_t value);
  void* (*get)     (void*, int32_t key);
  void* (*submit)  (void*, int32_t* sz, uint64_t* offset);
  void* (*complete)(void*, void*);

  void* (*cleanup) (void*);

};

struct sess_info {
  uint8_t hdr_type;
  uint8_t pad;
  uint64_t session_id;
  uint16_t thread_count;
  uint32_t block_sz;
} __attribute__ ((packed)) ;

struct file_info {
  union {
    uint16_t block_sz_significand;
    uint64_t offset;
  };
  union {
    uint8_t block_sz_exponent;
    uint64_t file_no_packed;
  };
} __attribute__ ((packed)) ;

struct file_info_end {
  uint8_t hdr_type;
  uint8_t type;
  uint16_t hdr_sz; // 4
  uint32_t hash;  // 8
  uint64_t file_no;
  uint64_t sz;   // 24
}__attribute__ ((packed)) ;

struct file_stat_type {

  // CAS to become owner of region
  uint64_t state        __attribute__ ((aligned(64)));

  uint64_t file_no;
  uint64_t bytes;

  uint64_t block_offset;
  uint64_t bytes_total;

  int32_t  fd;
  uint32_t position;
  uint32_t poison;

  // Reader atomically increments this to find next read location
  // Future: Writer sets to sz from fi_end message.
  // uint64_t block_offset __attribute__ ((aligned(64)));

  // Each time an I/O successfully completes this gets incremented
  // uint64_t bytes_total  __attribute__ ((aligned(64)));

  /*
  // Atomic XOR of block file_hash
  uint64_t crc          __attribute__ ((aligned(64)));
  */

}__attribute__ ((packed)) ;


void file_iotest( void* );
void file_iotest_finish();

void file_randrd( void* buf, int count );
void file_prng( void* buf, int sz );

struct file_object* file_memoryinit( void*, int );

void* file_posixget( void* arg, int32_t key );
void* file_posixset( void* arg, int32_t key, uint64_t value );

int file_posixinit( struct file_object* fob );
void* file_posixfetch( void* arg );
void file_posixflush( void* arg );

int file_uringinit( struct file_object* fob );
int file_dummyinit( struct file_object* fob );
// int shmem_init( struct file_object* fob );

int32_t file_hash( void* block, int sz, int seed );


struct file_stat_type* file_addfile(uint64_t fileno, int fd, uint32_t crc, int64_t);
struct file_stat_type* file_next( int id, struct file_stat_type* );
struct file_stat_type* file_wait( uint64_t fileno, struct file_stat_type* );

int64_t file_stat_getbytes( void *file_object, int fd );
uint64_t  file_iow_remove( struct file_stat_type* fs, int id );

int file_get_activeport( void* args );
void memset_avx( void* dst );


#endif
