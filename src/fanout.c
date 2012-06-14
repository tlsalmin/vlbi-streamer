//Create  THREADS threads to receive fanouts
//#define OUTPUT
#define MMAP_TECH

#ifdef MMAP_TECH
//volatile struct tpacket_hdr * ps_header_start;
#define RING_BLOCKSIZE 65536
#define RING_FRAME_SIZE 8192
#define RING_BLOCK_NR 4096
//Note that somewhere around 5650 bytes, the ksoftirqd starts
//hogging cpu, but after that it disappears.
/*
#define RING_BLOCKSIZE 4096
#define RING_BLOCK_NR 4
#define RING_FRAME_SIZE 2048
*/

#define RING_FRAME_NR  RING_BLOCKSIZE / RING_FRAME_SIZE * RING_BLOCK_NR
#endif

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <time.h>
#ifdef MMAP_TECH
#include <sys/mman.h>
#include <sys/poll.h>
#include <pthread.h>
#endif

#include <unistd.h>

#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <arpa/inet.h>
#ifdef HAVE_LINUX_NET_TSTAMP_H
#include <linux/net_tstamp.h>
#endif

#include <net/if.h>
#include "config.h"
#include "fanout.h"
#include "streamer.h"
#include "udp_stream.h"


//Gatherer specific options
struct fanout_opts
{
  int fd;
  int fanout_arg;
  struct opt_s *opt;
  int running;
  //char* filename;
  //char* device_name;
  //int root_pid;
  //int time;
  int fanout_type;
  //unsigned int optbits;
  struct tpacket_req req;
  struct tpacket_hdr * ps_header_start;
  struct tpacket_hdr * header;
  struct pollfd pfd;
  unsigned int total_captured_bytes;
  unsigned int incomplete;
  unsigned int dropped;
  unsigned int total_captured_packets;
};

int fanout_setup_socket(struct opt_s* opt, struct streamer_entity* se)
{
  struct fanout_opts *spec_ops =(struct fanout_opts *) malloc(sizeof(struct fanout_opts));
  int err; 
  se->opt = (void*)spec_ops;
  spec_ops->opt = opt;
  /*
  spec_ops->device_name = opt->device_name;
  spec_ops->filename = opt->filename;
  spec_ops->root_pid = opt->root_pid;
  spec_ops->time = opt->time;
  */
  //spec_ops->fanout_type = opt->fanout_type;
  //spec_ops->optbits = opt->optbits;

  //spec_ops->fanout_arg = opt->fanout_arg;
  //Open socket for AF_PACKET raw packet capture
  //spec_ops->fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_IP));
  spec_ops->fd = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_IP));
  //int err, fd = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_IP));
  struct sockaddr_ll ll;
  struct ifreq ifr;
  //int fanout_arg;
  //int o_rcvbuf;

  if (spec_ops->fd < 0) {
    perror("socket");
    return -1;
  }

  //Get the interface index
  memset(&ifr, 0, sizeof(ifr));
  strcpy(ifr.ifr_name, spec_ops->opt->device_name);
  err = ioctl(spec_ops->fd, SIOCGIFINDEX, &ifr);
  if (err < 0) {
    perror("SIOCGIFINDEX");
    return -1;
  }
#ifdef HAVE_LINUX_NET_TSTAMP_H
  //Stolen from http://seclists.org/tcpdump/2010/q2/99
  struct hwtstamp_config hwconfig;
  //struct ifreq ifr;

  memset(&hwconfig, 0, sizeof(hwconfig));
  hwconfig.tx_type = HWTSTAMP_TX_ON;
  hwconfig.rx_filter = HWTSTAMP_FILTER_ALL;

  memset(&ifr, 0, sizeof(ifr));
  strcpy(ifr.ifr_name, spec_ops->opt->device_name);
  ifr.ifr_data = (void *)&hwconfig;

  if(ioctl(spec_ops->fd, SIOCSHWTSTAMP,&ifr)<  0) {
    fprintf(stderr, "Cant set hardware timestamping");
    /*
       snprintf(handle->errbuf, PCAP_ERRBUF_SIZE,
       "can't set SIOCSHWTSTAMP %d: %d-%s",
       handle->fd, errno, pcap_strerror(errno));
       */
  }
#endif

  //Bind to a socket
  memset(&ll, 0, sizeof(ll));
  ll.sll_family = AF_PACKET;
  //ll.sll_protocol = ETH_P_ALL;
  ll.sll_ifindex = ifr.ifr_ifindex;
  err = bind(spec_ops->fd, (struct sockaddr *) &ll, sizeof(ll));
  if (err < 0) {
    perror("bind");
    return -1;
  }

  spec_ops->fanout_arg = ((spec_ops->opt->root_pid & 0xFFFF) | (PACKET_FANOUT_LB << 16));
  err = setsockopt(spec_ops->fd, SOL_PACKET, PACKET_FANOUT,
      &(spec_ops->fanout_arg), sizeof(spec_ops->fanout_arg));
  if (err) {
    perror("setsockopt");
    return -1;
  }
#ifdef HAVE_LINUX_NET_TSTAMP_H
  //set hardware timestamping
  int req = 0;
  req |= SOF_TIMESTAMPING_SYS_HARDWARE;
  setsockopt(spec_ops->fd, SOL_PACKET, PACKET_TIMESTAMP, (void *) &req, sizeof(req))
#endif
    //struct tpacket_hdr * ps_header_start;
    /*
       req.tp_block_size= 4096;
       req.tp_frame_size= 2048;
       req.tp_block_nr  = 4;
       req.tp_frame_nr  = 8;
    //Set a ringbuffer for mmap
    */
    //for MMAP_TECH-buffer
    //struct tpacket_req req;

    //Make the socket send packets to the ring
    spec_ops->req.tp_block_size = spec_ops->opt->packet_size*(spec_ops->opt->buf_num_elems);
  spec_ops->req.tp_frame_size =spec_ops->opt->packet_size;
  spec_ops->req.tp_block_nr =spec_ops->opt->n_threads; 
  spec_ops->req.tp_frame_nr =spec_ops->opt->buf_num_elems;
  err = setsockopt(spec_ops->fd, SOL_PACKET, PACKET_RX_RING, (void*) &(spec_ops->req), sizeof(spec_ops->req));
  if (err) {
    perror("PACKET_RX_RING failed");
    return -1;
  }

  //MMap the packet ring
  //TODO: Try MAP_LCOKED for writing to fs
  spec_ops->ps_header_start = mmap(0, RING_BLOCKSIZE*RING_BLOCK_NR, PROT_READ|PROT_WRITE, MAP_SHARED, spec_ops->fd, 0);
  if (!spec_ops->ps_header_start)
  {
    perror("mmap");
    return -1;
  }

  //struct pollfd pfd;
  //struct tpacket_hdr *header;
  //Prepare the polling struct
  spec_ops->pfd.fd = spec_ops->fd;
  spec_ops->pfd.revents = 0;
  spec_ops->pfd.events = POLLIN|POLLERR;

  spec_ops->header = (void *) spec_ops->ps_header_start;

  return 0;
}
void handle_captured_packets(unsigned long *i, struct fanout_opts * spec_ops, int full){
  while((spec_ops->header->tp_status & TP_STATUS_USER) | (full && (*i)<RING_FRAME_NR)){
    if (spec_ops->header->tp_status & TP_STATUS_COPY){
      spec_ops->incomplete++;
      spec_ops->total_captured_packets++;
    }
    else if (spec_ops->header->tp_status & TP_STATUS_LOSING){
      spec_ops->dropped++;
      spec_ops->total_captured_packets++;
    }
    else{
      spec_ops->total_captured_bytes += spec_ops->header->tp_len;
      spec_ops->total_captured_packets++;
    }
    //TODO:Packet processing

    //Release frame back to kernel use
    spec_ops->header->tp_status = 0;

    //Update header point
    //header = (header + 1) & ((struct tpacket_hdr *)(RING_FRAME_NR -1 ));
    if((*i)>=RING_FRAME_NR-1 && !full)
      (*i) = 0;
    else 
      (*i)++;
    spec_ops->header = (void *) spec_ops->ps_header_start + (*i) * RING_FRAME_SIZE;
  }
}

void *fanout_thread(void *specco)
{
  struct streamer_entity *se = (struct streamer_entity*)specco;
  struct fanout_opts *spec_ops = (struct fanout_opts *)se->opt;
  time_t t_start;
  //double time_left=0;
  int err = 0;
  spec_ops->total_captured_bytes = 0;
  spec_ops->total_captured_packets = 0;
  spec_ops->incomplete = 0;
  spec_ops->dropped = 0;
  spec_ops->running = 1;
  unsigned long i=0;

  time(&t_start);

  while(spec_ops->running){
  //while((time_left = ((double)spec_ops->time-difftime(time(NULL), t_start))) > 0){
    if(!(spec_ops->header->tp_status & TP_STATUS_USER)){
      //Change seconds to milliseconds in third param
      //err = poll(&(spec_ops->pfd), 1, time_left*1000);
      err = poll(&(spec_ops->pfd), 1, 1000);
      //usleep(25);
    }

    handle_captured_packets(&i, spec_ops, CHECK_UP_TO_NEXT_RESERVED);
    if(err < 0){
      perror("poll or read");
      //TODO: Handle error
    }

    //Testing sleep to reduce interrupts. System call anyway, so doesn't help
    //usleep(101);
  }
  //Go through whole buffer one last time
  //handle_captured_packets(&i, spec_ops, CHECK_UP_ALL);
  //handle_captured_packets(&i, spec_ops, CHECK_UP_TO_NEXT_RESERVED);

  fprintf(stderr, "Pid %d Completed without errors\n", getpid());
  //exit(0);
  pthread_exit(NULL);
}
void fanout_get_stats(void *opt, void *stats){
  struct fanout_opts *spec_ops = (struct fanout_opts *)opt;
  struct stats *stat = (struct stats * ) stats;
  stat->total_bytes += spec_ops->total_captured_bytes;
  stat->incomplete += spec_ops->incomplete;
  stat->dropped += spec_ops->dropped;
}
int close_fanout(void *opt, void *stats){
  struct fanout_opts *spec_ops = (struct fanout_opts *)opt;
  fanout_get_stats(opt,stats);

  //Only need to close socket according to 
  //http://www.mjmwired.net/kernel/Documentation/networking/packet_mmap.txt
  close(spec_ops->fd);

  //munmap(spec_ops->header, RING_BLOCKSIZE*RING_BLOCK_NR);
  free(spec_ops);
  return 0;
}
int fanout_init_fanout(void * opt, struct streamer_entity *se){
  se->init = fanout_setup_socket;
  se->close = close_fanout;
  se->get_stats = fanout_get_stats;
  se->close_socket = udps_close_socket;
  se->start = fanout_thread;
  se->stop = udps_stop;
  return se->init(opt,se);
}
