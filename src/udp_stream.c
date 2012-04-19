//64 MB ring buffer
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define ONLY_INCREMENT_TIMER
#include "config.h"
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
#ifdef MMAP_TECH
#include <sys/mman.h>
#include <sys/poll.h>
#endif

#include <pthread.h>

#ifdef HAVE_RATELIMITER
#include <time.h> 
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
//Moved to generic, shouldn't need anymore
//#include "ringbuf.h"
//#include "aiowriter.h"


//Gatherer specific options
struct opts
{
  unsigned int optbits;

  int running;
  int fd;
  char* device_name;
  int root_pid;
  int time;
  int port;
  int do_w_stuff_every;
#ifdef HAVE_RATELIMITER
  int *wait_nanoseconds;
  struct timespec *wait_last_sent;
#endif
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
  struct sockaddr_in *sin;
  size_t sinsize;
  //Used for bidirectional usage
  //int read;
  int (*handle_packet)(struct streamer_entity*,void*);
#ifdef CHECK_OUT_OF_ORDER
  //Lazy to use in handle_packet
  INDEX_FILE_TYPE last_packet;
#endif
  pthread_mutex_t * headlock;
  pthread_cond_t * iosignal;
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
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
  int is_blocked;
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
  //spec_ops->read = opt->read;
  spec_ops->optbits = opt->optbits;
  spec_ops->signal = &(opt->signal);
  spec_ops->running = 1;
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
  spec_ops->is_blocked = 0;
#endif
  /* TODO: This could be changed to a quarter or similar, since the writer will fall behind 	*/
  /* Since this only signals the writer 							*/
  spec_ops->do_w_stuff_every = opt->do_w_stuff_every;

  /*
   * If we're reading, recording entity should read the indicedata
   */
  if(spec_ops->optbits & READMODE){
#ifdef HAVE_RATELIMITER
    /* Making this a pointer, so we can later adjust it accordingly */
    spec_ops->wait_nanoseconds = &(opt->wait_nanoseconds);
    spec_ops->wait_last_sent = &(opt->wait_last_sent);
#endif
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
  if(spec_ops->optbits & CHECK_SEQUENCE)
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
  if(opt->socket != 0){
    spec_ops->fd = opt->socket;
    /* If we're in send-mode, we need to set the proper sockadd_in for sending 		*/
    /* TODO: Check if we can bind the socket to an ip beforehand so this wouldn't 	*/
    /* be needed									*/
    if(spec_ops->optbits & READMODE){
      spec_ops->sin = (struct sockaddr_in*) malloc(sizeof(struct sockaddr_in));
      //socklen_t len = sizeof(struct sockaddr_in);
      memset(spec_ops->sin, 0, sizeof(struct sockaddr_in));   
      spec_ops->sin->sin_family = AF_INET;           
      spec_ops->sin->sin_port = htons(spec_ops->port);    
      //TODO: check if IF binding helps
      spec_ops->sin->sin_addr.s_addr = opt->serverip;
      spec_ops->sinsize = sizeof(struct sockaddr_in);
#ifdef DEBUG_OUTPUT
      fprintf(stdout, "UDP_STREAMER: Initialized sockadd_in.\n");
#endif
    }

    //If we're the initializing thread, init socket and share
  }
  else{
    spec_ops->fd = socket(AF_INET, SOCK_DGRAM, 0);
    //if(!(spec_ops->optbits & READMODE))
      opt->socket = spec_ops->fd;

    /* TODO: Remove DO_W_STUFF_EVERY since packet size is defined at invocation */
    /* Changed from compile-time						*/
#ifdef DEBUG_OUTPUT
    fprintf(stdout, "UDP_STREAMER: Doing HD-write stuff every %d\n", spec_ops->do_w_stuff_every);
#endif

    if (spec_ops->fd < 0) {
      perror("socket");
      return NULL;;
    }


    if(spec_ops->device_name != NULL){
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
    if(spec_ops->optbits & READMODE){
      err = getsockopt(spec_ops->fd, SOL_SOCKET, SO_SNDBUF, &def, (socklen_t *) &len);
#ifdef DEBUG_OUTPUT
      fprintf(stdout, "UDP_STREAMER: Defaults: SENDBUF %d\n",def);
#endif
    }
    else{
      err = getsockopt(spec_ops->fd, SOL_SOCKET, SO_RCVBUF, &def, (socklen_t *) &len);
#ifdef DEBUG_OUTPUT
      fprintf(stdout, "UDP_STREAMER: Defaults: RCVBUF %d\n",def);
#endif
    }


    //prep port
    //struct sockaddr_in *addr = (struct sockaddr_in*) malloc(sizeof(struct sockaddr_in));
    spec_ops->sin = (struct sockaddr_in*) malloc(sizeof(struct sockaddr_in));
    //socklen_t len = sizeof(struct sockaddr_in);
    memset(spec_ops->sin, 0, sizeof(struct sockaddr_in));   
    spec_ops->sin->sin_family = AF_INET;           
    spec_ops->sin->sin_port = htons(spec_ops->port);    
    //TODO: check if IF binding helps
    if(spec_ops->optbits & READMODE){
#ifdef DEBUG_OUTPUT
      fprintf(stdout, "Connecting to %s\n", opt->hostname);
#endif
      spec_ops->sin->sin_addr.s_addr = opt->serverip;
      spec_ops->sinsize = sizeof(struct sockaddr_in);

      /*
	 err = inet_aton(opt->hostname, &(spec_ops->sin->sin_addr));
	 if(err ==0){
	 perror("UDP_STREAMER: Inet aton");
	 return NULL;
	 }
	 */
      //err = connect(spec_ops->fd, (struct sockaddr*) spec_ops->sin, sizeof(*(spec_ops->sin)));
      //err = bind(spec_ops->fd, (struct sockaddr*) spec_ops->sin, sizeof(*(spec_ops->sin)));
      //spec_ops->sin = addr;
    }
    else{
      spec_ops->sin->sin_addr.s_addr = INADDR_ANY;
      err = bind(spec_ops->fd, (struct sockaddr *) spec_ops->sin, sizeof(*(spec_ops->sin)));
      //free(addr);
    }

    //Bind to a socket
    if (err < 0) {
      perror("bind or connect");
      fprintf(stderr, "Port: %d\n", spec_ops->port);
      return NULL;
    }
#ifdef DEBUG_OUTPUT
    else
      fprintf(stdout, "Socket connected as %d ok\n", spec_ops->fd);
#endif

#ifdef HAVE_LINUX_NET_TSTAMP_H
    //set hardware timestamping
    int req = 0;
    req |= SOF_TIMESTAMPING_SYS_HARDWARE;
    setsockopt(spec_ops->fd, SOL_PACKET, PACKET_TIMESTAMP, (void *) &req, sizeof(req))
#endif
  }
  return spec_ops;
}
/* Not used after moving ringbuf to separate thread */
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
/* NOTE: not used anymore after moving ringbuf to separate threads */
//TODO: Implement as generic function on ringbuffer for changable backends
void flush_writes(struct opts *spec_ops){
  spec_ops->be->write(spec_ops->be, 1);
  sleep(2);
  //Check em
  spec_ops->be->write(spec_ops->be,0);
  //spec_ops->be->write(spec_ops->be,0);
}
void *sender_exit(struct opts *spec_ops, pthread_t * rbuf_thread){
  spec_ops->be->stop(spec_ops->be); 
  /* Wake the thread up if its asleep */
  pthread_mutex_lock(spec_ops->headlock);
  pthread_cond_signal(spec_ops->iosignal);
  pthread_mutex_unlock(spec_ops->headlock);

  pthread_join(*rbuf_thread,NULL);
  pthread_mutex_destroy(spec_ops->headlock);

  pthread_exit(NULL);
}
/*
 * Sending handler for an UDP-stream
 *
 * NOTE: pthreads requires this arguments function to be void* so no  struct streaming_entity
 *
 * This is almost the same as udp_handler, but making it bidirectional might have overcomplicated
 * the logic and lead to high probability of bugs
 *
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

  spec_ops->headlock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
  spec_ops->iosignal = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
  pthread_mutex_init(spec_ops->headlock, NULL);
  pthread_cond_init(spec_ops->iosignal, NULL);

  pthread_t rbuf_thread;
  spec_ops->be->init_mutex(spec_ops->be, spec_ops->headlock, spec_ops->iosignal);

#ifdef DEBUG_OUTPUT
  fprintf(stdout, "UDP_STREAMER: Starting ringbuf thread\n");
#endif 
  int rc = pthread_create(&rbuf_thread, NULL,spec_ops->be->write_loop, (void*)spec_ops->be);
  if (rc){
    printf("ERROR; return code from pthread_create() is %d\n", rc);
    exit(-1);
  }
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "UDP_STREAMER: Ringbuf thread started\n");
#endif
  /*
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "UDP_STREAMER: Filling buffer\n");
#endif
  spec_ops->be->write(spec_ops->be, 1);
  
  usleep(100);
  spec_ops->be->write(spec_ops->be, 0);
  */
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "UDP_STREAMER: Starting sender-thread for %lu packets\n", spec_ops->max_num_packets);
#endif

  /* The sending is currently organized as a safe, but suboptimal. The whole loop is iterated 	*/
  /* Every time we have sent a package, eventhought the current sender might have many packets 	*/
  /* It could send in a row, but there are possible deadlocks if we're not careful when 	*/
  /* implementing the more efficient sender							*/

  //While we have packets to send TODO: Change total_capture_packets name to bidirectional
  while(spec_ops->total_captured_packets < spec_ops->max_num_packets){
    void *buf;

    pthread_mutex_lock(spec_ops->headlock);
    while((buf = spec_ops->be->get_writebuf(spec_ops->be)) == NULL){
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
      spec_ops->is_blocked = 1;
#endif
      pthread_cond_wait(spec_ops->iosignal, spec_ops->headlock);
    }
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
    spec_ops->is_blocked = 0;
#endif

    pthread_mutex_lock(spec_ops->cumlock);
    /* Not yet time to send this package so go to sleep */
    while(*pindex != *(spec_ops->cumul)){
#ifdef MULTITHREAD_SEND_DEBUG
      fprintf(stdout, "MULTITHREAD_SEND_DEBUG: Going to wait. Owner of %lu and need %lu\n", *pindex, *(spec_ops->cumul));
#endif
      /* Check for error in send */
      if(*(spec_ops->cumul) < 0){
	fprintf(stderr, "UDP_STREAMER: Error in other thread. Exiting\n");
	return sender_exit(spec_ops, &rbuf_thread);
      }
      pthread_cond_wait(spec_ops->signal,spec_ops->cumlock);
    }
    /* TODO :This might be a lot faster if we peek the next packet also. The nature of the receiving
     * program indicates differently thought, but in practice quite many packages go to a single recv */
#ifdef MULTITHREAD_SEND_DEBUG
    fprintf(stdout, "MULTITHREAD_SEND_DEBUG: I have the floor. Owner of %lu and need %lu\n", *pindex, *(spec_ops->cumul));
#endif
    /* Send packet, increment and broadcast that the current packet needed has been incremented */
    //err = send(spec_ops->fd, buf, spec_ops->buf_elem_size, 0);
#ifdef HAVE_RATELIMITER
    if(spec_ops->optbits & WAIT_BETWEEN){
      long wait= 0;
      if(spec_ops->wait_last_sent->tv_sec == 0 && spec_ops->wait_last_sent->tv_nsec == 0){
#ifdef DEBUG_OUTPUT
	fprintf(stdout, "UDP_STREAMER: Initializing wait clock\n");
#endif
	clock_gettime(CLOCK_REALTIME, spec_ops->wait_last_sent);
      }
      else{
	/* waittime - (time_now - time_last) needs to be positive if we need to wait 	*/
	/* 10^6 is for conversion to microseconds. Done before CLOCKS_PER_SEC to keep	*/
	/* accuracy									*/
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	wait = (now.tv_sec*BILLION + now.tv_nsec) - (spec_ops->wait_last_sent->tv_sec*BILLION + spec_ops->wait_last_sent->tv_nsec);
#ifdef DEBUG_OUTPUT
	fprintf(stdout, "UDP_STREAMER: %ld ns has passed since last send\n", wait);
#endif
	if(wait < *(spec_ops->wait_nanoseconds)){
	  //int mysleep = ((*(spec_ops->wait_nanoseconds)-wait)*1000000)/CLOCKS_PER_SEC;
#ifdef DEBUG_OUTPUT
	  fprintf(stdout, "UDP_STREAMER: Sleeping %d ys before sending packet\n", (*(spec_ops->wait_nanoseconds) - wait)/1000);
#endif	
	  usleep((*(spec_ops->wait_nanoseconds) - wait)/1000);
	}
	//*(spec_ops->wait_last_sent) = clock();//*(spec_ops->wait_nanoseconds);
	//*(spec_ops->wait_last_sent) += *(spec_ops->wait_nanoseconds);
	/* If we've drifter a bit too far from the clock 'cause of a context switch etc */
	/*
	if(wait < -100)
	  *(spec_ops->wait_last_sent) = clock();
	else
	  *(spec_ops->wait_last_sent) += *(spec_ops->wait_nanoseconds);
	  */
#ifdef ONLY_INCREMENT_TIMER
	if(wait > *(spec_ops->wait_nanoseconds)){
#ifdef DEBUG_OUTPUT
	  fprintf(stdout, "UDP_STREAMER: Runaway wait. Resetting to now\n");
#endif
	  spec_ops->wait_last_sent->tv_sec = now.tv_sec;
	  spec_ops->wait_last_sent->tv_nsec = now.tv_nsec;
	}
	else{
	  if(spec_ops->wait_last_sent->tv_nsec + *(spec_ops->wait_nanoseconds) > BILLION){
	    spec_ops->wait_last_sent->tv_sec++;
	    spec_ops->wait_last_sent->tv_nsec = *(spec_ops->wait_nanoseconds)-(BILLION - spec_ops->wait_last_sent->tv_nsec);
	  }
	  else
	    spec_ops->wait_last_sent->tv_nsec += *(spec_ops->wait_nanoseconds);
	}
#else
	spec_ops->wait_last_sent->tv_sec = now.tv_sec;
	spec_ops->wait_last_sent->tv_nsec = now.tv_nsec;
#endif
      }
    }
#endif /* HAVE_RATELIMITER */
    err = sendto(spec_ops->fd, buf, spec_ops->buf_elem_size, 0, spec_ops->sin,spec_ops->sinsize);
#ifdef HAVE_RATELIMITER
    //*(spec_ops->wait_last_sent) = clock();
#endif


    /* Increment to the next sendable packet */
    *(spec_ops->cumul) += 1;
    if(err < 0){
      perror("Send packet");
      /* Set the cumul to -1 so we can inform other threads to quit */
      (*spec_ops->cumul) = -1;
      shutdown(spec_ops->fd, SHUT_RDWR);
      pthread_cond_broadcast(spec_ops->signal);
      pthread_mutex_unlock(spec_ops->cumlock);
      return sender_exit(spec_ops, &rbuf_thread);
      //TODO: How to handle error case? Either shut down all threads or keep on trying
      //pthread_exit(NULL);
      //break;
    }
    else{
#ifdef MULTITHREAD_SEND_DEBUG
      fprintf(stdout, "MULTITHREAD_SEND_DEBUG: incrementing pindex\n");
#endif
      pindex++;
      if(*pindex != *(spec_ops->cumul)){
#ifdef MULTITHREAD_SEND_DEBUG
	fprintf(stdout, "MULTITHREAD_SEND_DEBUG: Broadcasting\n");
#endif
	/* We don't have the next packet */
	  pthread_cond_broadcast(spec_ops->signal);
      }
      /* TODO: This a little bit of an extra unlock, but it'll do for now */
      pthread_mutex_unlock(spec_ops->cumlock);
      spec_ops->total_captured_bytes +=(unsigned int) err;
      spec_ops->total_captured_packets++;
    }
    i++;

    /* Sending the packets deemed more crucial so only releasing io mutex and signaling after
     * We've sent the package */
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
    if(i%spec_ops->do_w_stuff_every == 0 && spec_ops->be->is_blocked(spec_ops->be) == 1)// || err == 0)
#else
    if(i%spec_ops->do_w_stuff_every == 0)
#endif
	pthread_cond_signal(spec_ops->iosignal);
    /* Release io thread to check on head */
    pthread_mutex_unlock(spec_ops->headlock);
  }
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "UDP_STREAMER: Closing buffer thread\n");
#endif
  return sender_exit(spec_ops, &rbuf_thread);
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

  spec_ops->headlock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
  spec_ops->iosignal = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
  pthread_mutex_init(spec_ops->headlock, NULL);
  pthread_cond_init(spec_ops->iosignal, NULL);

  pthread_t rbuf_thread;
  spec_ops->be->init_mutex(spec_ops->be, spec_ops->headlock, spec_ops->iosignal);

#ifdef DEBUG_OUTPUT
  fprintf(stdout, "UDP_STREAMER: Starting ringbuf thread\n");
#endif
  int rc = pthread_create(&rbuf_thread, NULL,spec_ops->be->write_loop, (void*)spec_ops->be);
  if (rc){
    printf("ERROR; return code from pthread_create() is %d\n", rc);
    exit(-1);
  }
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "UDP_STREAMER: Ringbuf thread started\n");
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
    pthread_mutex_lock(spec_ops->headlock);
    while((buf = spec_ops->be->get_writebuf(spec_ops->be)) == NULL && spec_ops->running){
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
      spec_ops->is_blocked = 1;
#endif
#ifdef DEBUG_OUTPUT
      fprintf(stdout, "UDP_STREAMER: Buffer full. Going to sleep\n");
#endif

      pthread_cond_wait(spec_ops->iosignal, spec_ops->headlock);

#ifdef DEBUG_OUTPUT
      fprintf(stdout, "UDP_STREAMER: Wake up from your asleep\n");
#endif
    }

#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
    spec_ops->is_blocked = 0;
#endif
    /*
#ifdef DEBUG_OUTPUT
fprintf(stdout, "UDP_STREAMER: Got buffer to write to\n");
#endif
*/
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
      else{
	perror("RECV error");
	fprintf(stderr, "UDP_STREAMER: Buf was at %lu\n", (long unsigned)buf);
      }
      pthread_mutex_unlock(spec_ops->cumlock);
      pthread_mutex_unlock(spec_ops->headlock);
      spec_ops->running = 0;
      break;
    }
    /* Success! */
    else{
      /* Signal writer that we've processed some packets and 	*/
      /* it should wake up unless it's busy writing		*/
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
      if(i%spec_ops->do_w_stuff_every == 0 && spec_ops->be->is_blocked(spec_ops->be) == 1)
#else
	if(i%spec_ops->do_w_stuff_every == 0)
#endif
	  pthread_cond_signal(spec_ops->iosignal);
      /* Used buffer so we can release headlock */
      //nth_package = *(spec_ops->cumul);
      //if(spec_ops->running > 0){
      if(spec_ops->running){
	*daspot = *(spec_ops->cumul);
	*(spec_ops->cumul) += 1;
      }
      /* If we reserved a buf but didn't write to it, cancel it */
      /* TODO: Annoyingly complex. Think simpler!		*/
      else
	spec_ops->be->cancel_writebuf(spec_ops->be);
      pthread_mutex_unlock(spec_ops->headlock);
      pthread_mutex_unlock(spec_ops->cumlock);
#ifdef CHECK_OUT_OF_ORDER
      spec_ops->last_packet = *daspot;
#endif

      if(spec_ops->running){
      spec_ops->total_captured_bytes +=(unsigned int) err;
      spec_ops->total_captured_packets += 1;
      if(spec_ops->total_captured_packets < spec_ops->max_num_packets)
	daspot++;
      else{
	fprintf(stderr, "UDP_STREAMER: Out of space on index file\n");
	break;
      }
      if(spec_ops->handle_packet != NULL)
	spec_ops->handle_packet(se,buf);
      }
      //}
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
    //TODO: Handle packets at maybe every 10 packets or so
    i++;
  }
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "UDP_STREAMER: Closing buffer thread\n");
#endif
  return sender_exit(spec_ops, &rbuf_thread);

  /*
  spec_ops->be->stop(spec_ops->be); 
  pthread_mutex_lock(spec_ops->headlock);
  pthread_cond_signal(spec_ops->iosignal);
  pthread_mutex_unlock(spec_ops->headlock);

  pthread_join(rbuf_thread,NULL);
  pthread_mutex_destroy(spec_ops->headlock);

  fprintf(stdout, "UDP_STREAMER: Closing streamer thread\n");
  pthread_exit(NULL);
  */
}
void get_udp_stats(void *sp, void *stats){
  struct stats *stat = (struct stats * ) stats;
  struct opts *spec_ops = (struct opts*)sp;
  //stat->total_packets += spec_ops->total_captured_packets;
  stat->total_bytes += spec_ops->total_captured_bytes;
  stat->incomplete += spec_ops->incomplete;
  stat->dropped += spec_ops->dropped;
}
int close_udp_streamer(void *opt_own, void *stats){
  struct opts *spec_ops = (struct opts *)opt_own;
  get_udp_stats(opt_own,  stats);

  //close(spec_ops->fd);
  //close(spec_ops->rp->fd);

#ifdef DEBUG_OUTPUT
  fprintf(stdout, "UDP_STREAMER: Closed\n");
#endif
  /* TODO: Make this nicer */
  if(!(spec_ops->optbits & READMODE)){
    //fprintf(stderr, "WUTTT\n");
    if (spec_ops->be->recer->write_index_data != NULL && spec_ops->be->recer->get_filename(spec_ops->be->recer) != NULL){
#ifdef DEBUG_OUTPUT
      fprintf(stdout, "UDP_STREAMER: Writing index data to %s\n", spec_ops->be->recer->get_filename(spec_ops->be->recer));
#endif
      if (spec_ops->be->recer->write_index_data(spec_ops->be->recer->get_filename(spec_ops->be->recer),(long unsigned)spec_ops->buf_elem_size, (void*)spec_ops->packet_index, (long unsigned)spec_ops->total_captured_packets) <0){
	fprintf(stderr, "UDP_STREAMER: Index data write failed\n");
      }
#ifdef DEBUG_OUTPUT
      else
	fprintf(stdout, "UDP_STREAMER: Index file written OK\n");
#endif
    }
#ifdef DEBUG_OUTPUT
    else
      fprintf(stdout, "UDP_STREAMER: No index file writing set\n");
#endif
  }
  //stats->packet_index = spec_ops->packet_index;
  spec_ops->be->close(spec_ops->be, stats);
  //free(spec_ops->be);
  /* So if we're reading, just let the recorder end free the packet_index */
  if(!(spec_ops->optbits & READMODE)){
    free(spec_ops->packet_index);
    free(spec_ops->sin);
  }
  free(spec_ops->headlock);
  free(spec_ops->iosignal);
  free(spec_ops);
  return 0;
}
void udps_stop(struct streamer_entity *se){
  ((struct opts *)se->opt)->running = 0;
}
void udps_close_socket(struct streamer_entity *se){
  int ret = shutdown(((struct opts*)se->opt)->fd, SHUT_RDWR);
  if(ret <0)
    perror("Socket shutdown");
}
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
int udps_is_blocked(struct streamer_entity *se){
  return ((struct opts *)(se->opt))->is_blocked;
}
#endif
unsigned long udps_get_max_packets(struct streamer_entity *se){
  return ((struct opts*)(se->opt))->max_num_packets;
}
void udps_init_default(struct opt_s *opt, struct streamer_entity * se, struct buffer_entity *be){
  se->init = setup_udp_socket;
  se->close = close_udp_streamer;
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
  se->is_blocked = udps_is_blocked;
#endif
  se->get_stats = get_udp_stats;
  se->close_socket = udps_close_socket;
  se->get_max_packets = udps_get_max_packets;
  se->opt = se->init(opt, be);
}
void udps_init_udp_receiver(struct opt_s *opt, struct streamer_entity *se, struct buffer_entity *be){
  udps_init_default(opt,se,be);
  se->start = udp_streamer;
  se->stop = udps_stop;
}
void udps_init_udp_sender(struct opt_s *opt, struct streamer_entity *se, struct buffer_entity *be){
  udps_init_default(opt,se,be);
  se->start = udp_sender;
}
