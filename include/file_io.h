#include <stdbool.h>

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
  uint64_t truncate; // 32
  uint32_t flags;
  uint32_t fd; // 40
};

struct file_object {
  // Generic object that abstracts file_io operation to different engines
  int32_t QD;
  int32_t fd;

  uint32_t blk_sz;
  uint16_t id;
  uint16_t io_type;  // Posix, uring, ...

  int64_t head;
  int64_t tail;      // 32

  uint64_t bitmask;  // Requests don't come back in order
  int32_t io_flags;  // i.e. O_DIRECT, O_RDONLY
  int32_t io_ret;    // 48

  uint32_t thread_count;
  uint64_t pad1;
  uint32_t pad2;      // 64


  void*    pvdr;     // Provider internal ptr
  char*    args;

  int   (*open)    (const char*, int, ...);
  int   (*close)   (int);
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

/*
struct file_info {
  uint8_t hdr_type;
  uint8_t pad;
  uint32_t block_sz;
  uint32_t file_no;
  uint64_t offset;
};
*/

struct file_info {
  union {
    uint8_t hdr_type;
    uint64_t offset;
  };
  uint32_t block_sz;
  uint32_t file_no;
} __attribute__ ((packed)) ;

struct file_info_end {
  uint8_t hdr_type;
  uint8_t pad;
  uint16_t hdr_sz;
  uint16_t type;
  uint16_t workers;
  uint32_t hash;
  uint32_t file_no;
  uint64_t sz;
  uint16_t hash_is_valid; //28
  uint32_t pad2[9];

}__attribute__ ((packed)) ;

struct file_info_long {
  uint16_t hdr_type;
  uint16_t hdr_sz;
  uint16_t mode;
  uint16_t uid;

  uint16_t gid;
  uint16_t name_sz;
  uint32_t file_no;

  uint64_t file_sz;

  uint32_t mtime;
  uint32_t ctime; // 32


  uint8_t bytes[1024+4096];

} __attribute__ ((packed)) ;

struct file_stat_type {
  int fd;
  uint32_t file_no;
  int flushing;
  int workers;
  int pending;
  uint64_t worker_map;
  uint64_t bytes;
  char* name;

  uint32_t crc;
  uint32_t given_crc;
  uint64_t bytes_total;

  uint64_t block_offset __attribute__ ((aligned(64)));
  // Add oopsies (list of work that wasn't finished)
};

struct file_close_struct {
  uint64_t sz;
  uint32_t hash;
  uint32_t workers;
  uint32_t did_close;
  uint32_t file_no;
  uint32_t flushing;
  uint32_t given_crc;

  char fn[80];
};

struct file_name_type {
  char*    name;
  uint32_t fino;
  uint8_t  state;

  // state == 0; is empty
  // state & 1; name added
  // state & 2; file is in use
};


void file_lockinit();
int file_add( void*, uint64_t );
struct file_stat_type* file_next( struct file_object*, int, void* );
void file_persist( bool );

void file_iotest( void* );
void file_randrd( void* buf, int count );
void file_prng( void* buf, int sz );

int file_b64encode( void* dst, void* src, int len );
int file_b64decode( void* dst, void* src, int len );

struct file_object* file_memoryinit( void*, int );
struct file_stat_type* file_wait( struct file_object* fob, uint32_t file_no );

void* file_posixget( void* arg, int32_t key );
void* file_posixset( void* arg, int32_t key, uint64_t value );

int file_posixinit( struct file_object* fob );
void* file_posixfetch( void* arg );
void file_posixflush( void* arg );

int file_uringinit( struct file_object* fob );
int file_dummyinit( struct file_object* fob );
void* file_close( struct file_object* fob, struct file_stat_type* fs,
  uint64_t, uint32_t, bool, uint32_t, int );
int32_t file_hash( void* block, int sz, int seed );
void file_compute_totals( int );
void file_mark_worker( struct file_stat_type* fs, int id);
void file_add_bulk( void* arg, uint32_t count );






#endif
