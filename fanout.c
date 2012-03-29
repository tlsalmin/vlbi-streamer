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
#include "fanout.h"
#include "streamer.h"


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
  unsigned int optbits;
  struct tpacket_req req;
  struct tpacket_hdr * ps_header_start;
  struct tpacket_hdr * header;
  struct pollfd pfd;
  unsigned int total_captured_bytes;
  unsigned int incomplete;
  unsigned int dropped;
  unsigned int total_captured_packets;
};

void * setup_socket(struct opt_s* opt, struct buffer_entity* se)
{
  struct opts *spec_ops =(struct opts *) malloc(sizeof(struct opts));
  spec_ops->device_name = opt->device_name;
  spec_ops->filename = opt->filename;
  spec_ops->root_pid = opt->root_pid;
  spec_ops->time = opt->time;
  //spec_ops->fanout_type = opt->fanout_type;
  spec_ops->optbits = opt->optbits;

  //spec_ops->fanout_arg = opt->fanout_arg;
  int err; 
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
    return NULL;;
  }

  //Get the interface index
  memset(&ifr, 0, sizeof(ifr));
  strcpy(ifr.ifr_name, spec_ops->device_name);
  err = ioctl(spec_ops->fd, SIOCGIFINDEX, &ifr);
  if (err < 0) {
    perror("SIOCGIFINDEX");
    return NULL;
  }
#ifdef HAVE_LINUX_NET_TSTAMP_H
  //Stolen from http://seclists.org/tcpdump/2010/q2/99
  struct hwtstamp_config hwconfig;
  //struct ifreq ifr;

  memset(&hwconfig, 0, sizeof(hwconfig));
  hwconfig.tx_type = HWTSTAMP_TX_ON;
  hwconfig.rx_filter = HWTSTAMP_FILTER_ALL;

  memset(&ifr, 0, sizeof(ifr));
  strcpy(ifr.ifr_name, spec_ops->device_name);
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
    return NULL;
  }

#ifdef THREADED
  //Set the fanout option
  //TODO: Check if we can create the socket just once and then only set this
  //per thread
  spec_ops->fanout_arg = ((spec_ops->root_pid & 0xFFFF) | (PACKET_FANOUT_LB << 16));
  err = setsockopt(spec_ops->fd, SOL_PACKET, PACKET_FANOUT,
      &(spec_ops->fanout_arg), sizeof(spec_ops->fanout_arg));
  if (err) {
    perror("setsockopt");
    return NULL;
  }
#endif //THREADED
#ifdef HAVE_LINUX_NET_TSTAMP_H
  //set hardware timestamping
  int req = 0;
  req |= SOF_TIMESTAMPING_SYS_HARDWARE;
  setsockopt(spec_ops->fd, SOL_PACKET, PACKET_TIMESTAMP, (void *) &req, sizeof(req))
#endif
#ifdef MMAP_TECH
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
    spec_ops->req.tp_block_size = RING_BLOCKSIZE;
  spec_ops->req.tp_frame_size = RING_FRAME_SIZE;
  spec_ops->req.tp_block_nr = RING_BLOCK_NR;
  spec_ops->req.tp_frame_nr = RING_FRAME_NR;
  err = setsockopt(spec_ops->fd, SOL_PACKET, PACKET_RX_RING, (void*) &(spec_ops->req), sizeof(spec_ops->req));
  if (err) {
    perror("PACKET_RX_RING failed");
    return NULL;
  }

  //MMap the packet ring
  //TODO: Try MAP_LCOKED for writing to fs
  spec_ops->ps_header_start = mmap(0, RING_BLOCKSIZE*RING_BLOCK_NR, PROT_READ|PROT_WRITE, MAP_SHARED, spec_ops->fd, 0);
  if (!spec_ops->ps_header_start)
  {
    perror("mmap");
    return NULL;
  }

  //struct pollfd pfd;
  //struct tpacket_hdr *header;
  //Prepare the polling struct
  spec_ops->pfd.fd = spec_ops->fd;
  spec_ops->pfd.revents = 0;
  spec_ops->pfd.events = POLLIN|POLLERR;

  spec_ops->header = (void *) spec_ops->ps_header_start;

#endif //MMAP_TECH
  return spec_ops;
}
void handle_captured_packets(uint64_t *i, struct opts * spec_ops, int full){
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

void *fanout_thread(void *se)
{
  struct streamer_entity *be = (struct streamer_entity*)se;
  struct opts *spec_ops = (struct opts *)be->opt;
  time_t t_start;
  double time_left=0;
  spec_ops->total_captured_bytes = 0;
  spec_ops->total_captured_packets = 0;
  spec_ops->incomplete = 0;
  spec_ops->dropped = 0;
  uint64_t i=0;


  if (spec_ops->fd < 0)
    exit(spec_ops->fd);

  time(&t_start);

  while((time_left = ((double)spec_ops->time-difftime(time(NULL), t_start))) > 0){
    int err = 0;
#ifdef MMAP_TECH
    if(!(spec_ops->header->tp_status & TP_STATUS_USER)){
      //Change seconds to milliseconds in third param
      err = poll(&(spec_ops->pfd), 1, time_left*1000);
      //usleep(25);
    }

    handle_captured_packets(&i, spec_ops, CHECK_UP_TO_NEXT_RESERVED);
#else 
    char buf[1600];
    err = recv(spec_ops->fd, buf, BUFSIZE, 0);
#endif
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

#ifdef MMAP_TECH
#endif
  fprintf(stderr, "Pid %d Completed without errors\n", getpid());
  //exit(0);
  pthread_exit(NULL);
}
void get_stats(void *opt, void *stats){
  struct opts *spec_ops = (struct opts *)opt;
  struct stats *stat = (struct stats * ) stats;
  stat->total_bytes += spec_ops->total_captured_bytes;
  stat->incomplete += spec_ops->incomplete;
  stat->dropped += spec_ops->dropped;
}
int close_fanout(void *opt, void *stats){
  struct opts *spec_ops = (struct opts *)opt;
  get_stats(opt,stats);

  //Only need to close socket according to 
  //http://www.mjmwired.net/kernel/Documentation/networking/packet_mmap.txt
  close(spec_ops->fd);

  //munmap(spec_ops->header, RING_BLOCKSIZE*RING_BLOCK_NR);
  free(spec_ops);
  return 0;
}
