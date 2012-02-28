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

#define PACKET_NUM 1000000
#define BUFSIZE 65536
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

#include <net/if.h>
#include "fanout.h"
#include "streamer.h"



void * setup_socket(void* options)
{
  struct opt_s *opt = (struct opt_s *)options;
  struct opts *spec_ops =(struct opts *) malloc(sizeof(struct opts));
  spec_ops->device_name = opt->device_name;
  spec_ops->filename = opt->filename;
  spec_ops->root_pid = opt->root_pid;
  spec_ops->time = opt->time;
  spec_ops->fanout_type = opt->fanout_type;
  //spec_ops->fanout_arg = opt->fanout_arg;
  int err; 
  //Open socket for AF_PACKET raw packet capture
  spec_ops->fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_IP));
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
  spec_ops->fanout_arg = ((spec_ops->root_pid & 0xFFFF) | (spec_ops->fanout_type << 16));
  err = setsockopt(spec_ops->fd, SOL_PACKET, PACKET_FANOUT,
      &(spec_ops->fanout_arg), sizeof(spec_ops->fanout_arg));
  if (err) {
    perror("setsockopt");
    return NULL;
  }
#endif //THREADED
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
void handle_captured_packets(uint64_t *total_captured, uint64_t *total_captured_packets, uint64_t *incomplete, uint64_t *dropped, uint64_t *i, struct opts * spec_ops, int full){
  while((spec_ops->header->tp_status & TP_STATUS_USER) | (full && (*i)<RING_FRAME_NR)){
    if (spec_ops->header->tp_status & TP_STATUS_COPY){
      (*incomplete)++;
      (*total_captured_packets)++;
    }
    else if (spec_ops->header->tp_status & TP_STATUS_LOSING){
      (*dropped)++;
      (*total_captured_packets)++;
    }
    else{
      (*total_captured) += spec_ops->header->tp_len;
      (*total_captured_packets)++;
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

void *fanout_thread(void *opt)
{
  struct opts *spec_ops = (struct opts *)opt;
  time_t t_start;
  double time_left=0;
  uint64_t total_captured = 0;
  uint64_t total_captured_packets = 0;
  uint64_t incomplete = 0;
  uint64_t dropped = 0;
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
    }

    handle_captured_packets(&total_captured, &total_captured_packets, &incomplete, &dropped, &i, spec_ops, CHECK_UP_TO_NEXT_RESERVED);
#else 
    char buf[1600];
    err = recv(spec_ops->fd, buf, BUFSIZE, 0);
#endif
    if(err < 0){
      perror("poll or read");
      //TODO: Handle error
    }

  }
  //Go through whole buffer one last time
  handle_captured_packets(&total_captured,&total_captured_packets,  &incomplete, &dropped, &i, spec_ops, CHECK_UP_ALL);

#ifdef MMAP_TECH
  fprintf(stderr, "Captured: %u bytes, in %u packets, with %u dropped and %u incomplete\n", total_captured,total_captured_packets ,dropped, incomplete);
#endif
  //exit(0);
  pthread_exit(NULL);
}
int close_fanout(void *opt){
  struct opts *spec_ops = (struct opts *)opt;
  munmap(spec_ops->header, RING_BLOCKSIZE*RING_BLOCK_NR);
  close(spec_ops->fd);
  //TODO: close socket and ring
  free(spec_ops);
  return 0;
}
