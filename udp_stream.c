
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

#define DO_W_STUFF_EVERY 512

//Gatherer specific options
struct opts
{
  int fd;
  int fanout_arg;
  char* device_name;
  int root_pid;
  int time;
  int port;
  unsigned long max_num_packets;
  //void * packet_index;
  long unsigned int * cumul;
  pthread_mutex_t * cumlock;
  int* packet_index;
  //int id;
  //Moved to buffer_entity
  //void * rbuf;
  //struct rec_point * rp;
  struct buffer_entity * be;
  //Duplicate here, but meh
  int buf_elem_size;
  //Used for bidirectional usage
  int read;
  //struct sockaddr target;

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
  spec_ops->be = se;
  spec_ops->buf_elem_size = opt->buf_elem_size;
  //spec_ops->id = opt->tid++;
  spec_ops->cumul = &(opt->cumul);
  spec_ops->cumlock = &(opt->cumlock);
  spec_ops->max_num_packets = opt->max_num_packets;
  spec_ops->packet_index = (int*)malloc(sizeof(int)*opt->max_num_packets);
  spec_ops->read = opt->read;
  //spec_ops->packet_index = opt->packet_index;

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
    struct sockaddr_in *addr = (struct sockaddr_in*) malloc(sizeof(struct sockaddr_in));
    //socklen_t len = sizeof(struct sockaddr_in);
    memset(addr, 0, sizeof(struct sockaddr_in));   
    addr->sin_family = AF_INET;           
    addr->sin_port = htons(spec_ops->port);    
    //TODO: check if IF binding helps
    if(spec_ops->read == 1){
      addr->sin_addr = opt->inaddr;
      err = connect(spec_ops->fd, (struct sockaddr*) addr, sizeof(struct sockaddr_in));
    }
    else{
      addr->sin_addr.s_addr = INADDR_ANY;
      err = bind(spec_ops->fd, (struct sockaddr *) addr, sizeof(struct sockaddr_in));
    }

    //Bind to a socket
    if (err < 0) {
      perror("bind or connect");
      return NULL;
    }
    free(addr);

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
  int err;

  //TODO: We get into trouble here if DO_W_STUFF_EVERY is large
  //and we have to start sleeping to wait for io.
  err = spec_ops->be->write(spec_ops->be, 0);
  if (recv == 0 && err != WRITE_COMPLETE_DONT_SLEEP){
    err = spec_ops->be->wait((void*)spec_ops->be);
#ifdef DEBUG_OUTPUT
    fprintf(stdout, "UDP_STREAMER: Woke up\n");
#endif
    if(err < 0){
      perror("Waking from aio-sleep");
      return err;
    }
  }

  return err;
}
//TODO: Implement as generic function on aioringbuffer for changable backends
void flush_writes(struct opts *spec_ops){
  spec_ops->be->write(spec_ops->be, 1);
  sleep(2);
  //Check em
  spec_ops->be->write(spec_ops->be,0);
  //spec_ops->be->write(spec_ops->be,0);
}

void* udp_streamer(void *opt)
{
  int err = 0;
  int i=0;
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
    void * buf;
    long unsigned int nth_package;

    if((buf = spec_ops->be->get_writebuf(spec_ops->be)) != NULL){
      //TODO: Try a semaphore here to limit interrupt utilization.
      //Probably doesn't help
      //TODO: Timeout

      //Critical sec in logging n:th packet
      pthread_mutex_lock(spec_ops->cumlock);
      err = read(spec_ops->fd, buf, spec_ops->buf_elem_size);
      nth_package = *(spec_ops->cumul);
      *(spec_ops->cumul) += 1;
      pthread_mutex_unlock(spec_ops->cumlock);

      if(nth_package < spec_ops->max_num_packets){
	int  *daspot = spec_ops->packet_index + spec_ops->total_captured_packets*sizeof(int);
	*daspot = nth_package;
      }

      if(err < 0){
	fprintf(stdout, "RECV error");
	break;
      }
      spec_ops->total_captured_bytes +=(unsigned int) err;
      spec_ops->total_captured_packets += 1;
    }
    //If write buffer is full
    else{
      err = 0;
#ifdef DEBUG_OUTPUT
      fprintf(stdout, "UDP_STREAMER: Buffer full!\n");
#endif
    }


    //TODO: Handle packets at maybe every 10 packets or so
    i++;
    if(i%DO_W_STUFF_EVERY == 0 || err == 0)
      err = handle_packets_udp(err, spec_ops, time_left);

    if(err < 0){
      fprintf(stderr, "UDP_STREAMER: Packet handling failed\n");
      break;
    }

  }
  if(err >= 0)
    flush_writes(spec_ops);

  pthread_exit(NULL);
}
void get_udp_stats(struct opts *spec_ops, void *stats){
  struct stats *stat = (struct stats * ) stats;
  //stat->total_packets += spec_ops->total_captured_packets;
  stat->total_bytes += spec_ops->total_captured_bytes;
  stat->incomplete += spec_ops->incomplete;
  stat->dropped += spec_ops->dropped;
}
int close_udp_streamer(void *opt_own, void *stats){
  struct opts *spec_ops = (struct opts *)opt_own;
  get_udp_stats(spec_ops,  stats);

  close(spec_ops->fd);
  //close(spec_ops->rp->fd);

#ifdef DEBUG_OUTPUT
  fprintf(stdout, "UDP_STREAMER: Closed\n");
#endif
  if (spec_ops->be->write_index_data(spec_ops->be,(void*)spec_ops->packet_index, spec_ops->total_captured_bytes) <0)
    fprintf(stderr, "Index data write failed on id ");
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "Writing index data\n");
#endif
  //stats->packet_index = spec_ops->packet_index;
  spec_ops->be->close(spec_ops->be, stats);
  //free(spec_ops->be);
  free(spec_ops->packet_index);
  free(spec_ops);
  return 0;
}
