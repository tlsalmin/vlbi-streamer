#ifndef UDP_STREAMER
#define UDP_STREAMER
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
//int phandler_sequence(struct streamer_entity * se, void * buffer);

struct resq_info{
  int  *inc_before, *inc;
  void  *buf, *usebuf, *bufstart, *bufstart_before;
  struct buffer_entity * before;
  long current_seq;
  long seqstart_current;
  int i;
  int packets_per_second;
  /* Special if the packets are spaced for example every */
  /* fifth second.					*/
  int packetsecdif;
  int starting_second;
};

int udps_init_udp_receiver( struct opt_s *opt, struct streamer_entity *se);

int udps_init_udp_sender( struct opt_s *opt, struct streamer_entity *se);
struct udpopts
{
  int running;
  int fd;
  int fd_send;
  struct opt_s* opt;
  //long unsigned int * cumul;
  struct sockaddr_in *sin;
  struct sockaddr_in *sin_send;
  size_t sinsize;

  struct fileholder* last;
  //void* (*calc_bufpos)(void*,struct streamer_entity*,struct resq_info *);
  unsigned long missing;
  unsigned long total_captured_packets;
  unsigned long total_captured_bytes;
  unsigned long incomplete;
  unsigned long files_sent; 
  unsigned long out_of_order;
};
void udps_close_socket(struct streamer_entity *se);

void * calc_bufpos_vdif(void* header, struct streamer_entity* se, struct resq_info* resq);
void * calc_bufpos_mark5b(void* header, struct streamer_entity* se, struct resq_info* resq);
void*  calc_bufpos_udpmon(void* header, struct streamer_entity* se, struct resq_info* resq);

#endif //UDP_STREAMER
