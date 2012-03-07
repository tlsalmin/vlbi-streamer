#ifndef UDP_STREAMER
#define UDP_STREAMER
#define UDP_STREAM_THREADS 12
/*
#ifndef PACKET_FANOUT
#define PACKET_FANOUT		18
#define PACKET_FANOUT_HASH		0
#define PACKET_FANOUT_LB		1
#endif
*/
#include <net/if.h>
#include <poll.h>
#include <linux/if_packet.h>
#include "streamer.h"

void * setup_udp_socket(void *opt, struct recording_entity * re);
void * udp_streamer(void *opt);
void get_udp_stats(void *opt, void *stats);
int close_udp_streamer(void *opt,void *stats);
#endif //UDP_STREAMER
