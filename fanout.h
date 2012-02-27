#ifndef FANOUT
#define FANOUT
#ifndef PACKET_FANOUT
#define PACKET_FANOUT		18
#define PACKET_FANOUT_HASH		0
#define PACKET_FANOUT_LB		1
#endif
#define THREADED
#ifndef THREADED
#define THREADS 1
#else
#define THREADS 6
#endif
#include <net/if.h>
#include <poll.h>
#include <linux/if_packet.h>

//Gatherer specific options
struct opts
{
  int fd;
  int fanout_arg;
  char* filename;
  char* device_name;
  int root_pid;
  int time;
  int fanout_type;
  struct tpacket_req req;
  struct tpacket_hdr * ps_header_start;
  struct tpacket_hdr * header;
  struct pollfd pfd;
};
void * setup_socket(void *opt);
void *fanout_thread(void *opt);
int close_fanout(void *opt);
#endif //FANOUT
