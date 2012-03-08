
//64 MB ring buffer
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
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
#endif

#include <pthread.h>

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
//Moved to generic, shouldn't need anymore
//#include "aioringbuf.h"
//#include "aiowriter.h"


//Gatherer specific options
struct opts
{
  int fd;
  int fanout_arg;
  char* device_name;
  int root_pid;
  int time;
  int port;
  //Moved to buffer_entity
  //void * rbuf;
  //struct rec_point * rp;
  struct buffer_entity * recer;
  //Duplicate here, but meh
  int buf_elem_size;

  //Moved to main init
  /*
  //Functions for usage in modularized infrastructure
  void* (*init_buffer)(void*, int,int);
  int (*write)(void*,void*,int);
  void* get_writebuf(void *);
  */

  unsigned long int total_captured_bytes;
  unsigned long int incomplete;
  unsigned long int dropped;
  unsigned long int total_captured_packets;
};

void * setup_udp_socket(struct opt_s * opt, struct buffer_entity * se)
{
  int err;
  //struct opt_s *opt = (struct opt_s *)options;
  struct opts *spec_ops =(struct opts *) malloc(sizeof(struct opts));
  spec_ops->device_name = opt->device_name;
  spec_ops->root_pid = opt->root_pid;
  spec_ops->time = opt->time;
  spec_ops->recer = se;
  spec_ops->buf_elem_size = opt->buf_elem_size;

#ifdef DEBUG_OUTPUT
  fprintf(stdout, "UDP_STREAMER: Initializing ring buffers\n");
#endif

  spec_ops->port = opt->port;

  //If socket ready, just set it
  if(!opt->socket == 0){
    spec_ops->fd = opt->socket;
    //If we're the initializing thread, init socket and share
  }
  else{
    spec_ops->fd = socket(AF_INET, SOCK_DGRAM, 0);
    opt->socket = spec_ops->fd;


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
    //NOTE: Has no effect
    /* set SO_RCVLOWAT , the minimum amount received to return from recv*/
    /*
    //socklen_t optlen = BUFSIZE;
    buflength = BUFSIZE;
    setsockopt(spec_ops->fd, SOL_SOCKET, SO_RCVLOWAT, (char*)&buflength, sizeof(buflength));
    */

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
  }
  return spec_ops;
}
int handle_packets_udp(int recv, struct opts * spec_ops, double time_left){
  spec_ops->total_captured_bytes +=(unsigned int) recv;
  spec_ops->total_captured_packets += 1;
  int err;

  if (recv == 0){
    err = spec_ops->recer->wait((void*)spec_ops->recer);
#ifdef DEBUG_OUTPUT
    fprintf(stdout, "UDP_STREAMER: Woke up\n");
#endif
    if(err < 0){
      perror("Waking from aio-sleep");
      return err;
    }
  }
  else
    err = spec_ops->recer->write(spec_ops->recer, 0);

  return err;
}
//TODO: Implement as generic function on aioringbuffer for changable backends
void flush_writes(struct opts *spec_ops){
  /*
  int ret = rbuf_aio_write(&(spec_ops->rbuf), spec_ops->rp, FORCE_WRITE);
  aiow_wait_for_write((void*)spec_ops->rp);
  while(ret >0)
    ret -= (aiow_check((void*)spec_ops->rp, (void*)&(spec_ops->rbuf)));
    */
  spec_ops->recer->write(spec_ops->recer, 1);
}

void* udp_streamer(void *opt)
{
  struct opts *spec_ops = (struct opts *)opt;
  time_t t_start;
  double time_left=0;
  spec_ops->total_captured_bytes = 0;
  spec_ops->total_captured_packets = 0;
  spec_ops->incomplete = 0;
  spec_ops->dropped = 0;

  if (spec_ops->fd < 0)
    exit(spec_ops->fd);

  listen(spec_ops->fd, 2);

  time(&t_start);

  while((time_left = ((double)spec_ops->time-difftime(time(NULL), t_start))) > 0){
    int err = 0;
    void * buf;

    if((buf = spec_ops->recer->get_writebuf(spec_ops->recer)) != NULL){
      //TODO: Try a semaphore here to limit interrupt utilization.
      //Probably doesn't help
      err = read(spec_ops->fd, buf, spec_ops->buf_elem_size);
    }
    //If write buffer is full
    else{
      err = 0;
#ifdef DEBUG_OUTPUT
      fprintf(stdout, "UDP_STREAMER: Buffer full!\n");
#endif
    }

    if(err < 0){
      fprintf(stdout, "RECV error");
      //TODO: Handle error
      break;
    }

    //TODO: Handle packets at maybe every 10 packets or so
    err = handle_packets_udp(err, spec_ops, time_left);

    if(err < 0)
      break;

  }
  flush_writes(spec_ops);

  pthread_exit(NULL);
}
void get_udp_stats(struct opts *spec_ops, void *stats){
  //struct opts *spec_ops = (struct opts *)opt;
  struct stats *stat = (struct stats * ) stats;
  stat->total_packets += spec_ops->total_captured_packets;
  stat->total_bytes += spec_ops->total_captured_bytes;
  stat->incomplete += spec_ops->incomplete;
  stat->total_packets += spec_ops->dropped;
  stat->total_written += spec_ops->recer->rp->bytes_written;
}
int close_udp_streamer(void *opt_own, void *stats){
  struct opts *spec_ops = (struct opts *)opt_own;
  get_udp_stats(spec_ops,  stats);

  close(spec_ops->fd);
  //close(spec_ops->rp->fd);

  //rbuf_close(&(spec_ops->rbuf));
  spec_ops->recer->close(spec_ops->recer);
  //munmap(spec_ops->header, RING_BLOCKSIZE*RING_BLOCK_NR);
  //free(spec_ops->rp);
  free(spec_ops);
  return 0;
}
