#ifndef UDP_STREAMER
#define UDP_STREAMER
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

void * setup_udp_socket(void *opt);
void * udp_streamer(void *opt);
void get_udp_stats(void *opt, void *stats);
int close_udp_streamer(void *opt);
#endif //UDP_STREAMER
