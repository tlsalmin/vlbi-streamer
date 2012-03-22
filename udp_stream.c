
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

#define MULTITHREAD_SEND_DEBUG

//Gatherer specific options
struct opts
{
  int running;
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
  INDEX_FILE_TYPE* packet_index;
  //int id;
  //Moved to buffer_entity
  //void * rbuf;
  //struct rec_point * rp;
  struct buffer_entity * be;
  //Duplicate here, but meh
  int buf_elem_size;
  //Used for bidirectional usage
  int read;
  int (*handle_packet)(struct streamer_entity*,void*);
#ifdef CHECK_OUT_OF_ORDER
  //Lazy to use in handle_packet
  INDEX_FILE_TYPE last_packet;
#endif
#ifdef SPLIT_RBUF_AND_IO_TO_THREAD
  pthread_mutex_t * headlock;
  pthread_cond_t * iosignal;
#endif
  //struct sockaddr target;

  pthread_cond_t * signal;
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
#ifdef CHECK_OUT_OF_ORDER
  unsigned long int out_of_order;
#endif
};
int phandler_sequence(struct streamer_entity * se, void * buffer){
  return 0;
}
/*
 * TODO: These should take a streamer_entity and confrom to the initialization of 
 * buffer_entity and recording_entity
 */

void * setup_udp_socket(struct opt_s * opt, struct buffer_entity * se)
{
  int err, len, def;
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
  spec_ops->read = opt->read;
  spec_ops->signal = &(opt->signal);
  spec_ops->running = 1;

  /*
   * If we're reading, recording entity should read the indicedata
   */
  if(spec_ops->read){
    spec_ops->max_num_packets = spec_ops->be->recer->get_n_packets(spec_ops->be->recer);
    spec_ops->packet_index = spec_ops->be->recer->get_packet_index(spec_ops->be->recer);
  }
  else{
    spec_ops->max_num_packets = opt->max_num_packets;
    spec_ops->packet_index = (INDEX_FILE_TYPE*)malloc(sizeof(INDEX_FILE_TYPE)*opt->max_num_packets);
    }

  /*
   * TODO: Make this an array of function pointers and conform to other modules
   */ 
#ifdef CHECK_OUT_OF_ORDER
  if(opt->handle & CHECK_SEQUENCE)
    spec_ops->handle_packet = phandler_sequence;
#endif
  //spec_ops->packet_index = opt->packet_index;

  /*
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "UDP_STREAMER: Initializing ring buffers\n");
#endif
*/

  spec_ops->port = opt->port;

  //If socket ready, just set it
  if(!opt->socket == 0){
    spec_ops->fd = opt->socket;
    //If we're the initializing thread, init socket and share
  }
  else{
    spec_ops->fd = socket(AF_INET, SOCK_DGRAM, 0);
    opt->socket = spec_ops->fd;

#ifdef DEBUG_OUTPUT
  fprintf(stdout, "Doing HD-write stuff every %d\n", DO_W_STUFF_EVERY);
#endif

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
    /*
     * Setting the default receive buffer size. Taken from libhj create_udp_socket
     */
    len = sizeof(def);
    def=0;
    err = getsockopt(spec_ops->fd, SOL_SOCKET, SO_RCVBUF, &def, (socklen_t *) &len);
#ifdef DEBUG_OUTPUT
    fprintf(stdout, "Defaults: RCVBUF %d\n",def);
#endif


    //prep port
    struct sockaddr_in *addr = (struct sockaddr_in*) malloc(sizeof(struct sockaddr_in));
    //socklen_t len = sizeof(struct sockaddr_in);
    memset(addr, 0, sizeof(struct sockaddr_in));   
    addr->sin_family = AF_INET;           
    addr->sin_port = htons(spec_ops->port);    
    //TODO: check if IF binding helps
    if(spec_ops->read == 1){
#ifdef DEBUG_OUTPUT
      fprintf(stdout, "Connecting to %s\n", opt->hostname);
#endif
      addr->sin_addr.s_addr = opt->serverip;
      err = connect(spec_ops->fd, (struct sockaddr*) addr, sizeof(*addr));
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
#ifdef DEBUG_OUTPUT
    else
      fprintf(stdout, "Socket connected as %d ok\n", spec_ops->fd);
#endif
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
  /*
   * recv =0 means we didn't get a buffer to/from write/read to. If  we didn't
   * finish a write err != WRITE_COMPLETE_DONT_SLEEP so its better to sleep this
   * thread to avoid busy loop
   */
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
  /* Smarter timing here */

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
/*
 * NOTE: pthreads requires this arguments function to be void* so no  struct streaming_entity
 *
 * This is almost the same as udp_handler, but making it bidirectional might have overcomplicated
 * the logic and lead to high probability of bugs
 *
 * Sending handler for an UDP-stream
 */
void * udp_sender(void *opt){
  struct streamer_entity *be =(struct streamer_entity*) opt;
  struct opts *spec_ops = (struct opts *)be->opt;
  int err = 0;
  int i=0;
  INDEX_FILE_TYPE *pindex = spec_ops->packet_index;
  //Just use this to track how many we've sent TODO: Change name
  spec_ops->total_captured_packets = 0;

  if (spec_ops->fd < 0)
    exit(spec_ops->fd);

#ifdef DEBUG_OUTPUT
  fprintf(stdout, "UDP_STREAMER: Filling buffer\n");
#endif
  spec_ops->be->write(spec_ops->be, 1);
  
  usleep(100);
  spec_ops->be->write(spec_ops->be, 0);
  /* Fill the buffer up first */
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "UDP_STREAMER: Starting sender-thread\n");
#endif
  //While we have packets to send TODO: Change max_num_packets name
  while(spec_ops->total_captured_packets < spec_ops->max_num_packets){
    void * buf = spec_ops->be->get_writebuf(spec_ops->be);
    /* Read the indice of the current file */ 
    //INDEX_FILE_TYPE pindex = *(spec_ops->packet_index + spec_ops->total_captured_bytes * sizeof(INDEX_FILE_TYPE));

    pthread_mutex_lock(spec_ops->cumlock);
    /* Not yet time to send this package so go to sleep */
    while(*pindex > *(spec_ops->cumul)){
#ifdef MULTITHREAD_SEND_DEBUG
      fprintf(stdout, "MULTITHREAD_SEND_DEBUG: Going to wait. Owner of %lu and need %lu\n", *pindex, *(spec_ops->cumul));
#endif
      pthread_cond_wait(spec_ops->signal,spec_ops->cumlock);
    }
    /* TODO :This might be a lot faster if we peek the next packet also. The nature of the receiving
     * program indicates differently thought */
#ifdef MULTITHREAD_SEND_DEBUG
      fprintf(stdout, "MULTITHREAD_SEND_DEBUG: I have the floor. Owner of %lu and need %lu\n", *pindex, *(spec_ops->cumul));
#endif
    {
      /* Send packet, increment and broadcast that the current packet needed has been incremented */
      err = send(spec_ops->fd, buf, spec_ops->buf_elem_size, 0);
      *(spec_ops->cumul) += 1;
#ifdef MULTITHREAD_SEND_DEBUG
      fprintf(stdout, "MULTITHREAD_SEND_DEBUG: Broadcasting\n");
#endif
      pthread_cond_broadcast(spec_ops->signal);
    }
    pthread_mutex_unlock(spec_ops->cumlock);
    if(err < 0){
      perror("Send packet");
      pthread_exit(NULL);
      //break;
    }
    i++;
    if(i%DO_W_STUFF_EVERY == 0 || err == 0){
      err = handle_packets_udp(err, spec_ops, 0);

      if(err < 0){
	fprintf(stderr, "UDP_STREAMER: HD stuffing of buffer failed\n");
	break;
      }
    }
    spec_ops->total_captured_packets++;
#ifdef MULTITHREAD_SEND_DEBUG
      fprintf(stdout, "incrementing pindex\n");
#endif
    pindex += sizeof(INDEX_FILE_TYPE);
  }

  pthread_exit(NULL);
}

/*
 * Receiver for UDP-data
 */
void* udp_streamer(void *se)
{
  int err = 0;
  int i=0;
  struct streamer_entity *be =(struct streamer_entity*) se;
  struct opts *spec_ops = (struct opts *)be->opt;
  //time_t t_start;
  //double time_left=0;
  INDEX_FILE_TYPE *daspot = spec_ops->packet_index;
  spec_ops->total_captured_bytes = 0;
  spec_ops->total_captured_packets = 0;
#ifdef CHECK_OUT_OF_ORDER
  spec_ops->out_of_order = 0;
#endif
  spec_ops->incomplete = 0;
  spec_ops->dropped = 0;

#ifdef SPLIT_RBUF_AND_IO_TO_THREAD
  spec_ops->headlock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
  spec_ops->iosignal = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
  pthread_mutex_init(spec_ops->headlock, NULL);
  pthread_cond_init(spec_ops->iosignal, NULL);

  pthread_t rbuf_thread;
  spec_ops->be->init_mutex(spec_ops->be, spec_ops->headlock, spec_ops->iosignal);

#ifdef DEBUG_OUTPUT
  fprintf(stdout, "UDP_STREAMER: Starting ringbuf thread\n");
#endif
  /* Segfault from here */
  int rc = pthread_create(&rbuf_thread, NULL,spec_ops->be->write_loop, (void*)spec_ops->be);
  if (rc){
    printf("ERROR; return code from pthread_create() is %d\n", rc);
    exit(-1);
  }
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "UDP_STREAMER: Ringbuf thread started\n");
#endif 
  
#endif

  if (spec_ops->fd < 0)
    exit(spec_ops->fd);

  //listen(spec_ops->fd, 2);

  //time(&t_start);

#ifdef DEBUG_OUTPUT
  fprintf(stdout, "UDP_STREAMER: Starting stream capture\n");
#endif
  //while((time_left = ((double)spec_ops->time-difftime(time(NULL), t_start))) > 0){
  while(spec_ops->running){
    void * buf;
    //long unsigned int nth_package;

    /* This breaks separation of modules. Needs to be implemented in every receiver */
#ifdef SPLIT_RBUF_AND_IO_TO_THREAD
    pthread_mutex_lock(spec_ops->headlock);
    while((buf = spec_ops->be->get_writebuf(spec_ops->be)) == NULL && spec_ops->running){
      fprintf(stdout, "UDP_STREAMER: Buffer full. Going to sleep\n");
      pthread_cond_wait(spec_ops->iosignal, spec_ops->headlock);
      fprintf(stdout, "UDP_STREAMER: Wake up from your asleep\n");
    }
    /*
#ifdef DEBUG_OUTPUT
    fprintf(stdout, "UDP_STREAMER: Got buffer to write to\n");
#endif
*/
    {
#else
    if((buf = spec_ops->be->get_writebuf(spec_ops->be)) != NULL){
#endif

      //Try a semaphore here to limit interrupt utilization.
      //Probably doesn't help .. Actually worked really nicely to 
      //reduce Software interrupts on one core!
      //TODO: read doesn't timeout if we aren't receiving any packets

      //Critical sec in logging n:th packet
      pthread_mutex_lock(spec_ops->cumlock);
      err = read(spec_ops->fd, buf, spec_ops->buf_elem_size);
      /*
#ifdef DEBUG_OUTPUT
    fprintf(stdout, "UDP_STREAMER: receive of size %d\n", err);
#endif
*/
      if(err < 0){
	if(err == EINTR)
	  fprintf(stdout, "UDP_STREAMER: Main thread has shutdown socket\n");
	else
	  perror("RECV error");
	pthread_mutex_unlock(spec_ops->cumlock);
	break;
      }
      else{
#ifdef SPLIT_RBUF_AND_IO_TO_THREAD
	/* Signal writer that we've processed some packets and 	*/
	/* it should wake up unless it's busy writing		*/
	if(i%DO_W_STUFF_EVERY == 0)
	  pthread_cond_signal(spec_ops->iosignal);
	/* Used buffer so we can release headlock */
	pthread_mutex_unlock(spec_ops->headlock);
#endif
	//nth_package = *(spec_ops->cumul);
	*daspot = *(spec_ops->cumul);
	*(spec_ops->cumul) += 1;
	pthread_mutex_unlock(spec_ops->cumlock);
      }


      //Write the index of the received package
      //TODO: Make exit or alternative solution if we receive more packets.
      //Maybe write the package data to disk. Allocation extra space is easiest solution
      /*
      if(nth_package < spec_ops->max_num_packets){
	INDEX_FILE_TYPE  *daspot = spec_ops->packet_index + spec_ops->total_captured_packets*sizeof(int);
	*daspot = nth_package;
      }
      */
#ifdef CHECK_OUT_OF_ORDER
      spec_ops->last_packet = *daspot;
#endif

      spec_ops->total_captured_bytes +=(unsigned int) err;
      spec_ops->total_captured_packets += 1;
      if(spec_ops->total_captured_packets < spec_ops->max_num_packets)
	daspot += sizeof(INDEX_FILE_TYPE);
      else{
	fprintf(stderr, "UDP_STREAMER: Out of space on index file");
	break;
      }
    }
    //If write buffer is full
#ifndef SPLIT_RBUF_AND_IO_TO_THREAD
    else{
      err = 0;
#ifdef DEBUG_OUTPUT
      fprintf(stdout, "UDP_STREAMER: Buffer full!\n");
#endif
    }
#endif

    if(spec_ops->handle_packet != NULL)
      spec_ops->handle_packet(se,buf);

    //TODO: Handle packets at maybe every 10 packets or so
    i++;
#ifndef SPLIT_RBUF_AND_IO_TO_THREAD
    if(i%DO_W_STUFF_EVERY == 0 || err == 0){
      err = handle_packets_udp(err, spec_ops, time_left);
    }
    if(err < 0){
      fprintf(stderr, "UDP_STREAMER: Packet handling failed\n");
      break;
    }
#endif
  }
#ifdef SPLIT_RBUF_AND_IO_TO_THREAD
#ifdef DEBUG_OUTPUT
    fprintf(stdout, "UDP_STREAMER: Closing buffer thread\n");
#endif
    spec_ops->be->stop(spec_ops->be); 
    /* Wake the thread up if its asleep */
    pthread_mutex_lock(spec_ops->headlock);
    pthread_cond_signal(spec_ops->iosignal);
    pthread_mutex_unlock(spec_ops->headlock);

    pthread_join(rbuf_thread,NULL);
    pthread_mutex_destroy(spec_ops->headlock);
#else
  if(err >= 0)
    flush_writes(spec_ops);
#endif

  fprintf(stdout, "UDP_STREAMER: Closing streamer thread\n");
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

  //close(spec_ops->fd);
  //close(spec_ops->rp->fd);

#ifdef DEBUG_OUTPUT
  fprintf(stdout, "UDP_STREAMER: Closed\n");
#endif
  /* TODO: Make this nicer */
  if(!spec_ops->read)
    if (spec_ops->be->write_index_data != NULL && spec_ops->be->write_index_data(spec_ops->be,(void*)spec_ops->packet_index, spec_ops->total_captured_packets) <0)
      fprintf(stderr, "UDP_STREAMER: Index data write failed\n");
  //stats->packet_index = spec_ops->packet_index;
  spec_ops->be->close(spec_ops->be, stats);
  //free(spec_ops->be);
  free(spec_ops->packet_index);
#ifdef SPLIT_RBUF_AND_IO_TO_THREAD
  free(spec_ops->headlock);
  free(spec_ops->iosignal);
#endif
  free(spec_ops);
  return 0;
}
void udps_stop(struct streamer_entity *se, int i){
  ((struct opts *)se->opt)->running = 0;
  //If we're the first thread, close the socket
  if(i == 0){
    int ret = shutdown(((struct opts*)se->opt)->fd, SHUT_RDWR);
    if(ret <0)
      perror("Socket shutdown");
  }
}
void udps_init_udp_receiver(struct opt_s *opt, struct streamer_entity *se, struct buffer_entity *be){
  se->init = setup_udp_socket;
  se->start = udp_streamer;
  se->close = close_udp_streamer;
  se->opt = se->init(opt, be);
  se->stop = udps_stop;
  }
void udps_init_udp_sender(struct opt_s *opt, struct streamer_entity *se, struct buffer_entity *be){
  se->init = setup_udp_socket;
  se->start = udp_sender;
  se->close = close_udp_streamer;
  se->opt = se->init(opt, be);
  }
