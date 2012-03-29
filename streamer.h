#ifndef STREAMER
#define STREAMER
//Rate as in GB/s
#define RATE 10
#define B(x) (1 << x)

/* What buf entity to use. Used by buf_type*/ 
#define LOCKER_WRITER		0x0000000f 
#define WRITER_AIOW_RBUF 	B(0)
#define WRITER_DUMMY 		B(1)

/* What HD writer to use. Used by rec_type*/
#define LOCKER_REC		0x000000f0
#define REC_AIO 		B(4)
#define REC_DUMMY 		B(5)
#define REC_DEF 		B(6)
#define REC_SPLICER 		B(7)

/* How to capture packets. */
#define LOCKER_CAPTURE		0x00000f00
#define CAPTURE_W_FANOUT 	B(8)
#define CAPTURE_W_UDPSTREAM 	B(9)
#define CAPTURE_W_SPLICER 	B(10)
#define CAPTURE_W_TODO 		B(11)

/* How fanout works */
/*
#define LOCKER_FANOUT		0x0000f000
#define PACKET_FANOUT_HASH     	0x01 << 12
#define PACKET_FANOUT_LB        0x01 << 13
*/

/* Global stuff */
#define CHECK_SEQUENCE 		B(12)
#define ASYNC_WRITE		B(13)
#define READMODE		B(14)

#define MEM_GIG 4
#define BUF_ELEM_SIZE 8192
//#define BUF_ELEM_SIZE 32768
//Ok so lets make the buffer size 3GB every time
#define MAX_OPEN_FILES 32
#define DEBUG_OUTPUT
//Magic number TODO: Refactor so we won't need this
#define WRITE_COMPLETE_DONT_SLEEP 1337
/* The length of our indices. A week at 10Gb/s is 99090432000 packets for one thread*/
#define INDEX_FILE_TYPE unsigned long
/* TODO: Make the definition below work where used */
#define INDEX_FILE_PRINT lu
/* Moving the rbuf-stuff to its own thread */
#define SPLIT_RBUF_AND_IO_TO_THREAD


/* Enable if you don't want extra messaging to nonblocked processes */
#define CHECK_FOR_BLOCK_BEFORE_SIGNAL

//NOTE: Weird behaviour of libaio. With small integer here. Returns -22 for operation not supported
//But this only happens on buffer size > (atleast) 30000
//Lets make it write every 65536 KB(4096 byte aligned)(TODO: Increase when using write and read at the same time)
//#define HD_WRITE_SIZE 16777216
#define HD_WRITE_SIZE 1048576
//#define HD_WRITE_SIZE 134217728
//#define HD_WRITE_SIZE 33554432
//#define HD_WRITE_SIZE 262144
//#define HD_WRITE_SIZE 524288
//#define HD_WRITE_SIZE 65536

//#define DO_W_STUFF_EVERY (HD_WRITE_SIZE/BUF_ELEM_SIZE)
//etc for packet handling
#include <pthread.h>
#include <netdb.h> // struct hostent
struct opt_s
{
  char *filename;

  /* Lock that spans over all threads. Used for tracking packets 	*/
  /* by sequence number							*/
  long unsigned int  cumul;
  pthread_mutex_t cumlock;
  char *device_name;

  unsigned int optbits;
  int root_pid;
  unsigned int time;
  int port;
  int socket;
  int n_threads;
  int do_w_stuff_every;
  unsigned long max_num_packets;
  char * filenames[MAX_OPEN_FILES];

  /* Moved to optbits */
  //int capture_type;
  //int buf_type;
  //int rec_type;
  //int fanout_type;
  //int async;
  //int read;
  //int handle;

  /* Bloat TODO Find alternative place for this */
  INDEX_FILE_TYPE buf_elem_size;
  int buf_num_elems;
  //These two are a bit silly. Should be moved to use as a parameter
  int taken_rpoints;
  int tid;
  char * hostname;
  unsigned long serverip;
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
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
  int (*is_blocked)(struct buffer_entity*);
#endif
  //int (*handle_packet)(struct buffer_entity*, void *);
  struct recording_entity * recer;
  struct streamer_entity * se;
  //struct rec_point * rp;
};
struct recording_entity
{
  void * opt;
  int (*init)(struct opt_s * , struct recording_entity*);
  long (*write)(struct recording_entity*,void*,size_t);
  int (*wait)(struct recording_entity *);
  int (*close)(struct recording_entity*, void *);
  long (*check)(struct recording_entity*);
  int (*getfd)(struct recording_entity*);
  int (*get_w_flags)();
  int (*get_r_flags)();
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
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
  int (*is_blocked)(struct streamer_entity *se);
#endif
  /* TODO: Refactor streamer to use the same syntax as buffer and writer */
  struct buffer_entity *be;
};
struct stats
{
  unsigned long total_bytes;
  unsigned long total_written;
  unsigned long incomplete;
  unsigned long dropped;
  //Cheating here to keep infra consistent
  int * packet_index;
};
#endif
