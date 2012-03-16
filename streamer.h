#ifndef STREAMER
#define STREAMER
//Rate as in GB/s
#define RATE 1
#define WRITER_AIOW_RBUF 0
#define WRITER_DUMMY 1
#define WRITER_TODO 2
#define REC_AIO 0
#define REC_TODO 1
#define REC_DUMMY 2
#define MEM_GIG 4
//#define BUF_ELEM_SIZE 8192
#define BUF_ELEM_SIZE 32768
//Ok so lets make the buffer size 3GB every time
#define FORCE_WRITE 1
#define DONT_FORCE_WRITE 0
#define MAX_OPEN_FILES 32
#define BYTES_PER_ENTRY 2
#define DEBUG_OUTPUT
//Magic number TODO: Refactor so we won't need this
#define WRITE_COMPLETE_DONT_SLEEP 1337
#define INDEX_FILE_TYPE int
#define CHECK_SEQUENCE 1
#define DO_SOMETHING_ELSE 2
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
  void * packet_index;
  unsigned long max_num_packets;
  //struct rec_point * points;
  char * filenames[MAX_OPEN_FILES];
  int buf_type;
  int rec_type;
  INDEX_FILE_TYPE buf_elem_size;
  int buf_num_elems;
  int read;
  //These two are a bit silly. Should be moved to use as a parameter
  int taken_rpoints;
  int tid;
  struct in_addr inaddr;
  int handle;
  pthread_cond_t signal;
  //struct hostent he;
  //int f_flags;
};
struct buffer_entity
{
  void * opt;
  //Functions for usage in modularized infrastructure
  int (*init)(struct opt_s* , struct buffer_entity*);
  int (*write)(struct buffer_entity*,int);
  void* (*get_writebuf)(struct buffer_entity *);
  int (*wait)(struct buffer_entity *);
  int (*close)(struct buffer_entity*,void * );
  int (*write_index_data)(struct buffer_entity*, void*, int);
  //int (*handle_packet)(struct buffer_entity*, void *);
  struct recording_entity * recer;
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
  int (*write_index_data)(struct recording_entity*, void*, int);
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
