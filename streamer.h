#ifndef STREAMER
#define STREAMER
#define RATE 10
#define WRITER_AIOW_RBUF 0
#define WRITER_TODO 1
#define BUF_ELEM_SIZE 8192
#define BUF_NUM_ELEMS 8192

//#define DEBUG_OUTPUT
struct rec_point
{
  char *filename;
  int fd;
  int taken;
  long long offset;
  void * iostruct;
  unsigned long bytes_written;
  int latest_write_num;
  //int (*init)(void*);
  //int (*write)(void*);
};
struct opt_s
{
  char *filename;
  char *device_name;
  int capture_type;
  int root_pid;
  int fanout_type;
  unsigned int time;
  int port;
  int socket;
  int n_threads;
  struct rec_point * points;
  int rec_type;
};
struct recording_entity
{
  void * opt;
  //Functions for usage in modularized infrastructure
  void* (*init_buffer)(int,int);
  int (*write)(void*,struct rec_point*,int);
  void* (*get_writebuf)(void *);
  int (*wait)(void*);
  int (*close)(void *, struct rec_point*);
}

//Generic struct for a streamer entity
struct streamer_entity
{
  void *opt;
  void* (*open)(void*,struct recording_entity*);
  void* (*start)(void*);
  int (*close)(void*,void*);
};
struct stats
{	
  unsigned long total_bytes;
  unsigned long total_written;
  unsigned long incomplete;
  unsigned long dropped;
  unsigned long total_packets;
};
#endif
