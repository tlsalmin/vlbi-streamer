#ifndef STREAMER
#define STREAMER
#define RATE 10
#define WRITER_AIOW_RBUF 0
#define WRITER_TODO 1
#define REC_AIO 0
#define REC_TODO 1
#define BUF_ELEM_SIZE 8192
#define BUF_NUM_ELEMS 32768
#define FORCE_WRITE 1
#define DONT_FORCE_WRITE 0
#define MAX_OPEN_FILES 32
#define BYTES_PER_ENTRY 2
#define DEBUG_OUTPUT
#include <pthread.h>
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
  int buf_elem_size;
  int buf_num_elems;
  //These two are a bit silly. Should be moved to use as a parameter
  int taken_rpoints;
  int tid;
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
