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
#define THREADS 5
#endif
#define CHECK_UP_TO_NEXT_RESERVED 0
#define CHECK_UP_ALL 1
#include <net/if.h>
#include <poll.h>
#include <linux/if_packet.h>

void * setup_socket(void *opt);
void * fanout_thread(void *opt);
void get_stats(void *opt, void *stats);
int close_fanout(void *opt, void *stats);
#endif //FANOUT
