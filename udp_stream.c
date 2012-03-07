//Create  THREADS threads to receive fanouts
//#define OUTPUT
//#define MMAP_TECH

//64 MB ring buffer
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
//Moved to streamer.h TODO: Make choosable
//#define BUF_ELEM_SIZE 8192
//#define BUF_NUM_ELEMS 8192
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
#include <sys/stat.h>
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
#include "aioringbuf.h"
#include "aiowriter.h"


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
  //Moved to recording_entity
  //void * rbuf;
  struct rec_point * rp;
  struct recording_entity * recer;

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

void * setup_udp_socket(void* options, struct recording_entity * se)
{
  int i,err;
  int f_flags = O_WRONLY|O_DIRECT|O_NOATIME|O_NONBLOCK;
  struct stat statinfo;
  struct opt_s *opt = (struct opt_s *)options;
  struct opts *spec_ops =(struct opts *) malloc(sizeof(struct opts));
  spec_ops->device_name = opt->device_name;
  spec_ops->filename = opt->filename;
  spec_ops->root_pid = opt->root_pid;
  spec_ops->time = opt->time;
  spec_ops->recer = se;

#ifdef DEBUG_OUTPUT
  fprintf(stdout, "UDP_STREAMER: Initializing ring buffers\n");
#endif
  //Init the buffer and assign functions to use
  //Moved to recer
  //rbuf_init(&(spec_ops->rbuf), BUF_ELEM_SIZE, BUF_NUM_ELEMS);

  for(i=0;i<opt->n_threads;i++){
    if(!opt->points[i].taken){

      opt->points[i].taken = 1;
      spec_ops->rp = &(opt->points[i]);

      //TODO: Move all of this to to streamer.c
#ifdef DEBUG_OUTPUT
      fprintf(stdout, "UDP_STREAMER: Initializing write point\n");
#endif
      //Check if file exists
      err = stat(spec_ops->rp->filename, &statinfo);
      if (err < 0) 
	if (errno == ENOENT){
	  f_flags |= O_CREAT;
	  //fprintf(stdout, "file doesn't exist\");
	}


      //This will overwrite existing file.TODO: Check what is the desired default behaviour 
      spec_ops->rp->fd = open(spec_ops->rp->filename, f_flags, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
      if(spec_ops->rp->fd == -1){
	fprintf(stderr,"Error %s on %s\n",strerror(errno), spec_ops->rp->filename);
	return NULL;
      }
#ifdef DEBUG_OUTPUT
      fprintf(stdout, "UDP_STREAMER: File opened\n");
#endif
      //TODO: Set offset accordingly if file already exists. Not sure if
      //needed, since data consistency would take a hit anyway
      spec_ops->rp->offset = 0;
      //RATE = 10 Gb => RATE = 10*1024*1024*1024/8 bytes/s. Handled on n_threads
      //for s seconds.
      loff_t prealloc_bytes = (RATE*opt->time*1024)/(opt->n_threads*8);
      //Split kb/gb stuff to avoid overflow warning
      prealloc_bytes = prealloc_bytes*1024*1024;
      //set flag FALLOC_FL_KEEP_SIZE to precheck drive for errors
      err = fallocate(spec_ops->rp->fd, 0,0, prealloc_bytes);
      if(err == -1){
	fprintf(stderr, "Fallocate failed on %s", spec_ops->rp->filename);
	return NULL;
      }
#ifdef DEBUG_OUTPUT
      fprintf(stdout, "UDP_STREAMER: File preallocated\n");
#endif
      //Uses AIOWRITER atm. TODO: Make really generic, so you can change the backends
      aiow_init((void*)&(spec_ops->rbuf), (void*)spec_ops->rp);
#ifdef DEBUG_OUTPUT
      fprintf(stdout, "UDP_STREAMER: AIOW initialized\n");
#endif

      break;
    }
  }
  //spec_ops->fanout_type = opt->fanout_type;
  spec_ops->port = opt->port;
  //int buflength;

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

  //TODO: Somehow need to get away from 
  if (recv == 0){
    err = spec_ops->recer->wait((void*)spec_ops->rp);
#ifdef DEBUG_OUTPUT
    fprintf(stdout, "UDP_STREAMER: Woke up\n");
#endif
    if(err < 0){
      perror("Waking from aio-sleep");
      return err;
    }
  }
  else
    spec_ops->recer->write(spec_ops->recer->opt, spec_ops->rp, 0);
  //Moving everything to ringbuf or generic writer
  /*
  if(spec_ops->rbuf.ready_to_write < 1){
    if ((err = aiow_check((void*)spec_ops->rp, (void*)&(spec_ops->rbuf)))>0){
#ifdef DEBUG_OUTPUT
      fprintf(stdout, "UDP_STREAMER: %d Writes complete. Cleared write block\n", err);
#endif
      spec_ops->rbuf.ready_to_write += err;
    }
    else if (err < 0)
      fprintf(stderr, "UDP_STREAMER: AIOW check returned error %d", err);
  }
  //No space in buffer. TODO: Wait only waits a static time
  else if (recv == 0){
    err = aiow_wait_for_write((void*)spec_ops->rp);
#ifdef DEBUG_OUTPUT
    fprintf(stdout, "UDP_STREAMER: Woke up\n");
#endif
    if(err < 0){
      perror("Waking from aio-sleep");
      return err;
    }
  }
  else{
    err= rbuf_aio_write(&(spec_ops->rbuf), spec_ops->rp, DONT_FORCE_WRITE);
    if(err < 0){
      perror("UDP_STREAMER: Aiow write error");
      return err;
    }
#ifdef DEBUG_OUTPUT
    else if (err > 0)
      fprintf(stdout, "UDP_STREAMER: %d Write request submitted\n", err);
#endif
  }
  */
  return 0;
}
//TODO: Implement as generic function on aioringbuffer for changable backends
void flush_writes(struct opts *spec_ops){
  int ret = rbuf_aio_write(&(spec_ops->rbuf), spec_ops->rp, FORCE_WRITE);
  aiow_wait_for_write((void*)spec_ops->rp);
  while(ret >0)
    ret -= (aiow_check((void*)spec_ops->rp, (void*)&(spec_ops->rbuf)));
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

    if((buf = spec_ops->recer->get_writebuf(recer->opt)) != NULL){
      //TODO: Try a semaphore here to limit interrupt utilization.
      //Probably doesn't help
      err = read(spec_ops->fd, buf, BUF_ELEM_SIZE);
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
void get_udp_stats(void *opt, void *stats){
  struct opts *spec_ops = (struct opts *)opt;
  struct stats *stat = (struct stats * ) stats;
  stat->total_packets += spec_ops->total_captured_packets;
  stat->total_bytes += spec_ops->total_captured_bytes;
  stat->incomplete += spec_ops->incomplete;
  stat->total_packets += spec_ops->dropped;
  stat->total_written += spec_ops->rp->bytes_written;
}
int close_udp_streamer(void *opt_own, void *stats){
  struct opts *spec_ops = (struct opts *)opt_own;
  get_udp_stats(opt_own, stats);

  close(spec_ops->fd);
  close(spec_ops->rp->fd);

  //rbuf_close(&(spec_ops->rbuf));
  spec_ops->recer->close(recer->opt);
  //munmap(spec_ops->header, RING_BLOCKSIZE*RING_BLOCK_NR);
  free(spec_ops->rp);
  free(spec_ops);
  return 0;
}
