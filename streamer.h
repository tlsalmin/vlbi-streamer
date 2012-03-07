#ifndef STREAMER
#define STREAMER
#define RATE 10
#define WRITER_AIOW_RBUF 0
#define WRITER_TODO 1
#define BUF_ELEM_SIZE 8192
#define BUF_NUM_ELEMS 8192
#define FORCE_WRITE 0
#define DONT_FORCE_WRITE 1

#define DEBUG_OUTPUT
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
  int f_flags;
};
struct recording_entity
{
  void * opt;
  //Functions for usage in modularized infrastructure
  void* (*init_buffer)(int,int);
  int (*write)(struct recording_entity*,int);
  void* (*get_writebuf)(struct recording_entity *);
  int (*wait)(struct recording_entity *);
  int (*close)(struct recording_entity*);
  struct rec_point * rp;
};

//Generic struct for a streamer entity
struct streamer_entity
{
  void *opt;
  void* (*open)(struct opt_s *,struct recording_entity*);
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
