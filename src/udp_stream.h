#ifndef UDP_STREAMER
#define UDP_STREAMER
#define CHECK_OUT_OF_ORDER
//#define UDP_STREAM_THREADS 12
#include <net/if.h>
#include <poll.h>
#include <linux/if_packet.h>
#include "streamer.h"

      /*
       * TODO: Change the function names to udps_<name>
       */
void * setup_udp_socket(struct opt_s *opt, struct buffer_entity * be);
void * udp_sender(void * opt);
void * udp_streamer(void *opt);
void get_udp_stats(void *opt, void *stats);
void udps_stop(struct streamer_entity *se);
int close_udp_streamer(void *opt,void *stats);
int phandler_sequence(void * opts, void * buffer);
void udps_init_udp_receiver(struct opt_s *opt, struct streamer_entity *se, struct buffer_entity *be);
void udps_init_udp_sender(struct opt_s *opt, struct streamer_entity *se, struct buffer_entity *be);
#endif //UDP_STREAMER
