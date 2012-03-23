#ifndef STREAMER
#define STREAMER
//Rate as in GB/s
#define RATE 1
#define WRITER_AIOW_RBUF 0
#define WRITER_DUMMY 1
#define WRITER_TODO 4
#define REC_AIO 0
#define REC_TODO 1
#define REC_DUMMY 2
#define REC_DEF 3
#define MEM_GIG 4
#define BUF_ELEM_SIZE 8192
//#define BUF_ELEM_SIZE 32768
//Ok so lets make the buffer size 3GB every time
#define FORCE_WRITE 1
#define DONT_FORCE_WRITE 0
#define MAX_OPEN_FILES 32
#define BYTES_PER_ENTRY 2
#define DEBUG_OUTPUT
//Magic number TODO: Refactor so we won't need this
#define WRITE_COMPLETE_DONT_SLEEP 1337
/* The length of our indices. A week at 10Gb/s is 99090432000 packets for one thread*/
#define INDEX_FILE_TYPE unsigned long
/* TODO: Make the definition below work where used */
#define INDEX_FILE_PRINT lu
/* Moving the rbuf-stuff to its own thread */
#define SPLIT_RBUF_AND_IO_TO_THREAD
#define CHECK_SEQUENCE 1
#define DO_SOMETHING_ELSE 2

//NOTE: Weird behaviour of libaio. With small integer here. Returns -22 for operation not supported
//But this only happens on buffer size > (atleast) 30000
//Lets make it write every 65536 KB(4096 byte aligned)(TODO: Increase when using write and read at the same time)
//#define HD_WRITE_SIZE 16777216
#define HD_WRITE_SIZE 1048576
//#define HD_WRITE_SIZE 33554432
//#define HD_WRITE_SIZE 262144
//#define HD_WRITE_SIZE 524288

#define DO_W_STUFF_EVERY (HD_WRITE_SIZE/BUF_ELEM_SIZE)
//etc for packet handling
#include <pthread.h>
#include <netdb.h> // struct hostent
struct opt_s
{
  char *filename;
  long unsigned int  cumul;
  pthread_mutex_t cumlock;
  char *device_name;
  int capture_type;
  int root_pid;
  int fanout_type;
  unsigned int time;
  int port;
  int socket;
  int n_threads;
  //void * packet_index;
  unsigned long max_num_packets;
  //struct rec_point * points;
  char * filenames[MAX_OPEN_FILES];
  int buf_type;
  int rec_type;
  /* Bloat TODO Find alternative place for this */
  int async;
  INDEX_FILE_TYPE buf_elem_size;
  int buf_num_elems;
  int read;
  //These two are a bit silly. Should be moved to use as a parameter
  int taken_rpoints;
  int tid;
  char * hostname;
  unsigned long serverip;
  int handle;
  pthread_cond_t signal;
  //struct hostent he;
  //int f_flags;
};
struct buffer_entity
{
  void * opt;
  //Functions for usage in modularized infrastructure
  /* TODO: This is getting bloated. Check what we're actually still using */
  int (*init)(struct opt_s* , struct buffer_entity*);
  int (*write)(struct buffer_entity*,int);
  void* (*get_writebuf)(struct buffer_entity *);
  int (*wait)(struct buffer_entity *);
  int (*close)(struct buffer_entity*,void * );
  //int (*write_index_data)(struct buffer_entity*, void*, int);
  void* (*write_loop)(void *);
  void (*stop)(struct buffer_entity*);
  void (*init_mutex)(struct buffer_entity *, void*,void*);
  //int (*handle_packet)(struct buffer_entity*, void *);
  struct recording_entity * recer;
  struct streamer_entity * se;
  //struct rec_point * rp;
};
struct recording_entity
{
  void * opt;
  int (*init)(struct opt_s * , struct recording_entity*);
  int (*write)(struct recording_entity*,void*,size_t);
  int (*wait)(struct recording_entity *);
  int (*close)(struct recording_entity*, void *);
  int (*check)(struct recording_entity*);
  int (*write_index_data)(const char*, int, void*, int);
  const char* (*get_filename)(struct recording_entity *re);
  /* Bloat bloat bloat. TODO: Add a common filestruct or something*/
  unsigned long (*get_n_packets)(struct recording_entity*);
  INDEX_FILE_TYPE* (*get_packet_index)(struct recording_entity*);
  struct buffer_entity *be;
};

//Generic struct for a streamer entity
struct streamer_entity
{
  void *opt;
  void* (*init)(struct opt_s *,struct buffer_entity*);
  void* (*start)(void*);
  int (*close)(void*,void*);
  void (*stop)(struct streamer_entity *se);
  void (*close_socket)(struct streamer_entity *se);
};
struct stats
{
  unsigned long total_bytes;
  unsigned long total_written;
  unsigned long incomplete;
  unsigned long dropped;
  //Cheating here to keep infra constitent
  int * packet_index;
  //unsigned long total_packets;
};
#endif
