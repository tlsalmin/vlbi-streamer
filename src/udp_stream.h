#ifndef UDP_STREAMER
#define UDP_STREAMER
//#define CHECK_OUT_OF_ORDER
//#define UDP_STREAM_THREADS 12
#include <net/if.h>
#include <poll.h>
#include <linux/if_packet.h>
#include "streamer.h"

/*
 * TODO: Change the function names to udps_<name>
 */
int setup_udp_socket(struct opt_s *opt, struct streamer_entity *se);
void * udp_sender(void * opt);
void * udp_receiver(void *opt);
void get_udp_stats(void *opt, void *stats);
void udps_stop(struct streamer_entity *se);
int close_udp_streamer(void *opt,void *stats);
int phandler_sequence(struct streamer_entity * se, void * buffer);

int udps_init_udp_receiver( struct opt_s *opt, struct streamer_entity *se);

int udps_init_udp_sender( struct opt_s *opt, struct streamer_entity *se);
struct udpopts
{
  int running;
  int fd;
  struct opt_s* opt;
  //long unsigned int * cumul;
  struct sockaddr_in *sin;
  size_t sinsize;
  int (*handle_packet)(struct streamer_entity*,void*);
#ifdef CHECK_OUT_OF_ORDER
  //Lazy to use in handle_packet
  INDEX_FILE_TYPE last_packet;
#endif

  unsigned long int total_captured_bytes;
  unsigned long int incomplete;
  unsigned long int dropped;
  unsigned long int total_captured_packets;
  unsigned long files_sent; 
#ifdef CHECK_OUT_OF_ORDER
  unsigned long int out_of_order;
#endif
};
void udps_close_socket(struct streamer_entity *se);

#endif //UDP_STREAMER
