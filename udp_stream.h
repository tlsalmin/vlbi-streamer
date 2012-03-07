#ifndef UDP_STREAMER
#define UDP_STREAMER
//#define UDP_STREAM_THREADS 12
#include <net/if.h>
#include <poll.h>
#include <linux/if_packet.h>
#include "streamer.h"

void * setup_udp_socket(struct opt_s *opt, struct recording_entity * re);
void * udp_streamer(void *opt);
void get_udp_stats(void *opt, void *stats);
int close_udp_streamer(void *opt,void *stats);
#endif //UDP_STREAMER
