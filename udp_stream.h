#ifndef UDP_STREAMER
#define UDP_STREAMER
#define CHECK_OUT_OF_ORDER
//#define UDP_STREAM_THREADS 12
#include <net/if.h>
#include <poll.h>
#include <linux/if_packet.h>
#include "streamer.h"

void * setup_udp_socket(struct opt_s *opt, struct buffer_entity * be);
void * udp_streamer(void *opt);
void get_udp_stats(void *opt, void *stats);
int close_udp_streamer(void *opt,void *stats);
int phandler_sequence(void * opts, void * buffer);
#endif //UDP_STREAMER
