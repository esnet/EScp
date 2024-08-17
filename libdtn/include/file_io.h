#include <stdbool.h>
#include <stdint.h>
#include <sys/uio.h>
#include <dirent.h>


#ifndef __FILE_IO_DOT_H__
#define __FILE_IO_DOT_H__

#define FIHDR_SHORT 16
#define FIHDR_CINIT 1
#define FIHDR_CRYPT 2
#define FIHDR_META  3

#define FIIO_POSIX 1
#define FIIO_URING 2
#define FIIO_DUMMY 3
#define FIIO_SHMEM 4

#define FOB_SZ 1
#define FOB_OFFSET 2
#define FOB_BUF 3
#define FOB_FD 4
#define FOB_TRUNCATE 5
#define FOB_COMPRESSED 6
#define FOB_HASH 7

#define FIO_COMPRESS_MARGIN (256*1024)

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
  uint32_t compressed;
  uint32_t compress_offset;
  uint32_t hash;
};

struct file_object {
  // Generic object that abstracts file_io operation to different engines
  int32_t QD;
  uint8_t hugepages;
  uint8_t compression;
  uint8_t is_compressed;
  uint8_t do_hash;

  uint32_t blk_sz;
  uint16_t id;
  uint16_t io_type;  // Posix, uring, ...


  int64_t  head;
  int64_t  tail;     // 32

  uint64_t pad11;    //
  int32_t  io_flags; // i.e. O_DIRECT, O_RDONLY
  int32_t  io_ret;   // 48

  uint32_t thread_count;
  uint8_t  sparse;
  uint8_t  pad3;
  uint16_t pad2;
  uint64_t pad1;     // 64


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

  void* (*preserve)   (int32_t fd, uint32_t mode, uint32_t uid, uint32_t gid, int64_t atim_sec, int64_t atim_nano, int64_t mtim_sec, int64_t mtim_nano);
  void* (*cleanup) (void*);

};

struct file_info {
  uint64_t offset;
  uint64_t file_no;
  uint32_t sz;
} __attribute__ ((packed)) ;

struct file_stat_type {

  // CAS to become owner of region
  uint64_t state        __attribute__ ((aligned(64)));

  uint64_t file_no;
  uint64_t bytes;        // File sz from file meta data

  uint64_t block_offset; // 32
  uint64_t bytes_total;  // Incremented after successful I/O

  int32_t  fd;
  uint32_t position;     // Self referential
  uint32_t poison;
  uint32_t crc;
  uint64_t pad; // 64

  int64_t atim_sec;
  int64_t atim_nano;
  int64_t mtim_sec;
  int64_t mtim_nano; // 96

  uint32_t uid;
  uint32_t gid;
  uint32_t mode;
  uint32_t pad2;    // 112

  uint64_t pad3[2]; // 128



} __attribute__ ((packed)) ;


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


struct file_stat_type* file_addfile(uint64_t fileno, int fd,
  int64_t sz, int64_t as, int64_t an, int64_t ms, int64_t mn);
struct file_stat_type* file_next( int id, struct file_stat_type* );
struct file_stat_type* file_wait( uint64_t fileno, struct file_stat_type*, int id);

int64_t file_stat_getbytes( void *file_object, int fd );
uint64_t  file_iow_remove( struct file_stat_type* fs, int id );

int file_get_activeport( void* args );
void memcpy_avx( void* dst, void* src );
void memset_avx( void* dst );
void file_incrementtail();



#endif
