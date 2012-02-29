//Create  THREADS threads to receive fanouts
//#define OUTPUT
//#define MMAP_TECH

#define PACKET_NUM 1000000
#define BUFSIZE 65536
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

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
#include <netinet/in.h>

#include <net/if.h>
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
  int port;
  /*
  int fanout_type;
  struct tpacket_req req;
  struct tpacket_hdr * ps_header_start;
  struct tpacket_hdr * header;
  struct pollfd pfd;
  */
  unsigned int total_captured_bytes;
  unsigned int incomplete;
  unsigned int dropped;
  unsigned int total_captured_packets;
};

void * setup_udp_socket(void* options)
{
  struct opt_s *opt = (struct opt_s *)options;
  struct opts *spec_ops =(struct opts *) malloc(sizeof(struct opts));
  spec_ops->device_name = opt->device_name;
  spec_ops->filename = opt->filename;
  spec_ops->root_pid = opt->root_pid;
  spec_ops->time = opt->time;
  //spec_ops->fanout_type = opt->fanout_type;
  spec_ops->port = opt->socket;

  //spec_ops->fanout_arg = opt->fanout_arg;
  int err; 
  //Open socket for AF_PACKET raw packet capture
  //spec_ops->fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_IP));
  spec_ops->fd = socket(AF_INET, SOCK_DGRAM, 0);
  //int err, fd = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_IP));
  //int fanout_arg;
  //int o_rcvbuf;

  if (spec_ops->fd < 0) {
    perror("socket");
    return NULL;;
  }

  //struct sockaddr_ll ll;
  struct ifreq ifr;
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

  //prep port
  struct sockaddr_in my_addr;
  //socklen_t len = sizeof(struct sockaddr_in);
  memset(&my_addr, 0, sizeof(my_addr));   
  my_addr.sin_family = AF_INET;           
  my_addr.sin_port = htons(spec_ops->port);    
  //TODO: check if IF binding helps
  my_addr.sin_addr.s_addr = INADDR_ANY;

  //Bind to a socket
  err = bind(spec_ops->fd, (struct sockaddr *) &my_addr, sizeof(my_addr));
  if (err < 0) {
    perror("bind");
    return NULL;
  }

#ifdef HAVE_LINUX_NET_TSTAMP_H
  //set hardware timestamping
  int req = 0;
  req |= SOF_TIMESTAMPING_SYS_HARDWARE;
  setsockopt(spec_ops->fd, SOL_PACKET, PACKET_TIMESTAMP, (void *) &req, sizeof(req))
#endif

  //struct pollfd pfd;
  //struct tpacket_hdr *header;
  //Prepare the polling struct
  /*
  spec_ops->pfd.fd = spec_ops->fd;
  spec_ops->pfd.revents = 0;
  spec_ops->pfd.events = POLLIN|POLLERR;

  spec_ops->header = (void *) spec_ops->ps_header_start;

*/
  return spec_ops;
}
void handle_packets_udp(int recv, struct opts * spec_ops){
  spec_ops->total_captured_bytes += recv;
  spec_ops->total_captured_packets += 1;
}

void* udp_streamer(void *opt)
{
  struct opts *spec_ops = (struct opts *)opt;
  time_t t_start;
  double time_left=0;
  //struct sockaddr_in client;
  //unsigned int clilen = sizeof(client);
  int fd;
  spec_ops->total_captured_bytes = 0;
  spec_ops->total_captured_packets = 0;
  spec_ops->incomplete = 0;
  spec_ops->dropped = 0;
  //uint64_t i=0;
  char buf[BUFSIZE];


  if (spec_ops->fd < 0)
    exit(spec_ops->fd);

  listen(spec_ops->fd, 2);

  //if((fd = accept(spec_ops->fd,(struct sockaddr *)&client,&clilen)) > 0){
  time(&t_start);

  while((time_left = ((double)spec_ops->time-difftime(time(NULL), t_start))) > 0){
    int err = 0;

    err = recv(spec_ops->fd, (void*)&buf, BUFSIZE, 0);

    handle_packets_udp(err, spec_ops);
    fprintf(stdout, "Got %d size packet\n", err);
    if(err < 0){
      fprintf(stdout, "RECV error");
      //TODO: Handle error
    }

  }

  pthread_exit(NULL);
}
void get_udp_stats(void *opt, void *stats){
  struct opts *spec_ops = (struct opts *)opt;
  struct stats *stat = (struct stats * ) stats;
  stat->total_packets += spec_ops->total_captured_packets;
  stat->total_bytes += spec_ops->total_captured_bytes;
  stat->incomplete += spec_ops->incomplete;
  stat->total_packets += spec_ops->dropped;
}
int close_udp_streamer(void *opt){
  struct opts *spec_ops = (struct opts *)opt;

  //Only need to close socket according to 
  //http://www.mjmwired.net/kernel/Documentation/networking/packet_mmap.txt
  close(spec_ops->fd);

  //munmap(spec_ops->header, RING_BLOCKSIZE*RING_BLOCK_NR);
  free(spec_ops);
  return 0;
}
