#ifndef STREAMER
#define STREAMER
#define RATE 10
struct rec_point
{
  char *filename;
  int fd;
  int taken;
  void * iostruct;
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
};

//Generic struct for a streamer entity
struct streamer_entity
{
  void* (*open)(void*);
  void* (*start)(void*);
  int (*close)(void*,void*);
  void *opt;
};
struct stats
{	
  unsigned long total_bytes;
  unsigned long incomplete;
  unsigned long dropped;
  unsigned long total_packets;
};
#endif
