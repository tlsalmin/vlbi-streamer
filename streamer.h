#ifndef STREAMER
#define STREAMER
struct opt_s
{
  char *filename;
  char *device_name;
  int capture_type;
  int root_pid;
  int fanout_type;
  int time;
  int socket;
};

//Generic struct for a streamer entity
struct streamer_entity
{
  void* (*open)(void*);
  void* (*start)(void*);
  int (*close)(void*);
  void *opt;
};
struct stats
{	
  unsigned int total_bytes;
  unsigned int incomplete;
  unsigned int dropped;
  unsigned int total_packets;
};
#endif
