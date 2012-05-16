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
#include <sys/mman.h> //FOR MMAP and poll
#include <sys/poll.h>

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
#include "udp_stream.h"
/* Most of TPACKET-stuff is stolen from codemonkey blog */
/* http://codemonkeytips.blogspot.com/			*/
/// Offset of data from start of frame
#define PKT_OFFSET      (TPACKET_ALIGN(sizeof(struct tpacket_hdr)) + \
                         TPACKET_ALIGN(sizeof(struct sockaddr_ll)))
#define INIT_ERROR return -1;
//#define SHOW_PACKET_METADATA;
#define BIND_WITH_PF_PACKET
//#define CHECK_ERR(x) do{if(err!=0){perror(x);E(x);return -1;}else{D(x);}}while(0)
#define CHECK_ERR_CUST(x,y) do{if(y!=0){perror(x);E("ERROR:"x);return -1;}else{D(x);}}while(0)
#define CHECK_ERR(x) CHECK_ERR_CUST(x,err)
#define CHECK_ERR_NONNULL(val,mes) do{if(val==NULL){perror(mes);E(mes);return -1;}else{D(mes);}}while(0)
#define CHECK_ERR_LTZ(x) do{if(err<0){perror(x);E(x);return -1;}else{D(x);}}while(0)
//do {if(err != 0){perror(x);E(x);return -1;}else{D(x)}}while(0)


//Gatherer specific options
int phandler_sequence(struct streamer_entity * se, void * buffer){
  return 0;
}
void udps_close_socket(struct streamer_entity *se){
  int ret = shutdown(((struct udpopts*)se->opt)->fd, SHUT_RDWR);
  if(ret <0)
    perror("Socket shutdown");
}
int udps_bind_port(struct udpopts * spec_ops){
  int err=0;
  //prep port
  //struct sockaddr_in *addr = (struct sockaddr_in*) malloc(sizeof(struct sockaddr_in));
  spec_ops->sin = (struct sockaddr_in*) malloc(sizeof(struct sockaddr_in));
  //socklen_t len = sizeof(struct sockaddr_in);
  memset(spec_ops->sin, 0, sizeof(struct sockaddr_in));   
  spec_ops->sin->sin_family = AF_INET;           
  spec_ops->sin->sin_port = htons(spec_ops->opt->port);    
  //TODO: check if IF binding helps
  if(spec_ops->opt->optbits & READMODE){
#if(DEBUG_OUTPUT)
    fprintf(stdout, "Connecting to %s\n", spec_ops->opt->hostname);
#endif
    spec_ops->sin->sin_addr.s_addr = spec_ops->opt->serverip;
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
    //if(!(spec_ops->opt->optbits & USE_RX_RING))
    err = bind(spec_ops->fd, (struct sockaddr *) spec_ops->sin, sizeof(*(spec_ops->sin)));
    //free(addr);
    CHECK_ERR("Bind or connect");
  }

  //Bind to a socket
  return 0;
}
int udps_bind_rx(struct udpopts * spec_ops){
  struct tpacket_req req;
  int err;

  //TODO: Fix do_w_stuff_every so we can set it here accordingly to block_size. 
  /* I guess I need to just calculate this .. */
  /* FIX: Just set frame size as n-larger so its 16 divisable. */

  unsigned long total_mem_div_blocksize = (spec_ops->opt->buf_elem_size*spec_ops->opt->buf_num_elems*spec_ops->opt->n_threads)/(spec_ops->opt->do_w_stuff_every);
  //req.tp_block_size = spec_ops->opt->buf_elem_size*(spec_ops->opt->buf_num_elems)/4096;
  req.tp_block_size = spec_ops->opt->do_w_stuff_every;
  req.tp_frame_size = spec_ops->opt->buf_elem_size;
  req.tp_frame_nr = spec_ops->opt->buf_num_elems*(spec_ops->opt->n_threads);
  //req.tp_block_nr = spec_ops->opt->n_threads;
  req.tp_block_nr = total_mem_div_blocksize;

  D("Block size: %d Frame size: %d Block nr: %d Frame nr: %d Max order:",,req.tp_block_size, req.tp_frame_size, req.tp_block_nr, req.tp_frame_nr);

  err = setsockopt(spec_ops->fd, SOL_PACKET, PACKET_RX_RING, (void *) &req, sizeof(req));
  CHECK_ERR("RX_RING SETSOCKOPT");

  //int flags = MAP_ANONYMOUS|MAP_SHARED;
  int flags = MAP_SHARED;
  if(spec_ops->opt->optbits & USE_HUGEPAGE)
    flags |= MAP_HUGETLB;

  spec_ops->opt->buffer = mmap(0, req.tp_block_size*req.tp_block_nr, PROT_READ|PROT_WRITE , flags, spec_ops->fd,0);
  if((long)spec_ops->opt->buffer <= 0){
    perror("Ring MMAP");
    return -1;
  }

  //spec_ops->sin->sin_family = PF_PACKET;           
  struct sockaddr_ll ll;
  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  strcpy(ifr.ifr_name, spec_ops->opt->device_name);
  err = ioctl(spec_ops->fd, SIOCGIFINDEX, &ifr);
  CHECK_ERR("SIOCGIFINDEX");

  //Bind to a socket
  memset(&ll, 0, sizeof(ll));
  ll.sll_family = AF_PACKET;
  ll.sll_protocol = htons(ETH_P_ALL);
  ll.sll_ifindex = ifr.ifr_ifindex;
  err = bind(spec_ops->fd, (struct sockaddr *) &ll, sizeof(ll));
  CHECK_ERR("Bind to IF");

  return 0;
}
int udps_common_init_stuff(struct streamer_entity *se)
{
  int err,len,def;
  struct udpopts * spec_ops = se->opt;
  if(spec_ops->opt->device_name != NULL){
    //struct sockaddr_ll ll;
    struct ifreq ifr;
    //Get the interface index
    memset(&ifr, 0, sizeof(ifr));
    strcpy(ifr.ifr_name, spec_ops->opt->device_name);
    err = ioctl(spec_ops->fd, SIOCGIFINDEX, &ifr);
    CHECK_ERR_LTZ("Interface index find");

    D("Binding to %s",, spec_ops->opt->device_name);
    err = setsockopt(spec_ops->fd, SOL_SOCKET, SO_BINDTODEVICE, (void*)&ifr, sizeof(ifr));
    CHECK_ERR("Bound to NIC");


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

  err  = ioctl(spec_ops->fd, SIOCSHWTSTAMP,&ifr);
  CHECK_ERR_LTZ("HW timestamping");
#endif
  /* TODO: Drop bad size packets as in */
  /* ret = setsockopt(iface->fd, SOL_PACKET, PACKET_LOSS, &val, sizeof(val)); */
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
  if(spec_ops->opt->optbits & READMODE){
    err = getsockopt(spec_ops->fd, SOL_SOCKET, SO_SNDBUF, &def, (socklen_t *) &len);
    CHECK_ERR("SNDBUF size");
  }
  else{
    err = getsockopt(spec_ops->fd, SOL_SOCKET, SO_RCVBUF, &def, (socklen_t *) &len);
    CHECK_ERR("RCVBUF size");
  }

#ifdef SO_NO_CHECK
  if(spec_ops->opt->optbits & READMODE){
    const int sflag = 1;
    err = setsockopt(spec_ops->fd, SOL_SOCKET, SO_NO_CHECK, &sflag, sizeof(sflag));
    CHECK_ERR("UDPCHECKSUM");

  }
#endif

#ifdef HAVE_LINUX_NET_TSTAMP_H
  //set hardware timestamping
  int req = 0;
  req |= SOF_TIMESTAMPING_SYS_HARDWARE;
  err = setsockopt(spec_ops->fd, SOL_PACKET, PACKET_TIMESTAMP, (void *) &req, sizeof(req));
  CHECK_ERR("HWTIMESTAMP");
#endif
  return 0;
}

int setup_udp_socket(struct opt_s * opt, struct streamer_entity *se)
{
  int err;
  struct udpopts *spec_ops =(struct udpopts *) malloc(sizeof(struct udpopts));
  se->opt = (void*)spec_ops;

  spec_ops->opt = opt;
  spec_ops->running = 1;

  if(spec_ops->opt->optbits & READMODE){
#ifdef HAVE_RATELIMITER
    /* Making this a pointer, so we can later adjust it accordingly */
    //spec_ops->wait_nanoseconds = &(opt->wait_nanoseconds);
    //spec_ops->wait_last_sent = &(opt->wait_last_sent);
#endif
    //TODO: Get packet index in main by checking all writers
  }
#ifdef CHECK_OUT_OF_ORDER
  if(spec_ops->opt->optbits & CHECK_SEQUENCE)
    spec_ops->handle_packet = phandler_sequence;
#endif

  /* TODO Works with AF_INET but should use PF_PACKET? */
#ifdef BIND_WITH_PF_PACKET
  if(spec_ops->opt->optbits & USE_RX_RING){
    spec_ops->fd = socket(PF_PACKET, SOCK_DGRAM, htons(ETH_P_IP));
    D("Socket initialized with PF_PACKET");
  }
  else
#endif
  {
    spec_ops->fd = socket(AF_INET, SOCK_DGRAM, 0);
    D("Socket initialized as AF_INET");
  }
  //if(!(spec_ops->optbits & READMODE))
  opt->socket = spec_ops->fd;


  if (spec_ops->fd < 0) {
    perror("socket");
    INIT_ERROR
  }
  err = udps_common_init_stuff(se);
  CHECK_ERR("Common init");


  if(spec_ops->opt->optbits & USE_RX_RING){
    err = udps_bind_rx(spec_ops);
    CHECK_ERR("BIND RX");
  }
  else{
    err = udps_bind_port(spec_ops);
    CHECK_ERR("Bind port");
  }

  /* MMap the ring for rx-ring */


  return 0;
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
  struct udpopts *spec_ops = (struct udpopts *)be->opt;
  int err = 0;
  int i=0;
  //INDEX_FILE_TYPE *pindex = spec_ops->packet_index;
  //Just use this to track how many we've sent TODO: Change name
  spec_ops->total_captured_packets = 0;


  /*
     spec_ops->headlock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
     spec_ops->iosignal = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
     pthread_mutex_init(spec_ops->headlock, NULL);
     pthread_cond_init(spec_ops->iosignal, NULL);
     */

  //pthread_t rbuf_thread;
  //spec_ops->be->init_mutex(spec_ops->be, spec_ops->headlock, spec_ops->iosignal);

  /*
#if(DEBUG_OUTPUT)
fprintf(stdout, "UDP_STREAMER: Starting ringbuf thread\n");
#endif 
int rc = pthread_create(&rbuf_thread, NULL,spec_ops->be->write_loop, (void*)spec_ops->be);
if (rc){
printf("ERROR; return code from pthread_create() is %d\n", rc);
exit(-1);
}
*/
  /*
#if(DEBUG_OUTPUT)
fprintf(stdout, "UDP_STREAMER: Filling buffer\n");
#endif
spec_ops->be->write(spec_ops->be, 1);

usleep(100);
spec_ops->be->write(spec_ops->be, 0);
*/

  /* The sending is currently organized as a safe, but suboptimal. The whole loop is iterated 	*/
  /* Every time we have sent a package, eventhought the current sender might have many packets 	*/
  /* It could send in a row, but there are possible deadlocks if we're not careful when 	*/
  /* implementing the more efficient sender							*/

  //While we have packets to send TODO: Change total_capture_packets name to bidirectional
  //while(spec_ops->total_captured_packets < spec_ops->max_num_packets){
  //TODO!


  //TODO: Reimplement when receive-side running again
  /*
     while(1){
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
while(*pindex != *(spec_ops->cumul)){
#ifdef MULTITHREAD_SEND_DEBUG
fprintf(stdout, "MULTITHREAD_SEND_DEBUG: Going to wait. Owner of %lu and need %lu\n", *pindex, *(spec_ops->cumul));
#endif
if(*(spec_ops->cumul) < 0){
fprintf(stderr, "UDP_STREAMER: Error in other thread. Exiting\n");
return sender_exit(spec_ops, &rbuf_thread);
}
pthread_cond_wait(spec_ops->signal,spec_ops->cumlock);
}
#ifdef MULTITHREAD_SEND_DEBUG
fprintf(stdout, "MULTITHREAD_SEND_DEBUG: I have the floor. Owner of %lu and need %lu\n", *pindex, *(spec_ops->cumul));
#endif
#ifdef HAVE_RATELIMITER
if(spec_ops->optbits & WAIT_BETWEEN){
long wait= 0;
if(spec_ops->wait_last_sent->tv_sec == 0 && spec_ops->wait_last_sent->tv_nsec == 0){
#if(DEBUG_OUTPUT)
fprintf(stdout, "UDP_STREAMER: Initializing wait clock\n");
#endif
clock_gettime(CLOCK_REALTIME, spec_ops->wait_last_sent);
}
else{
  // waittime - (time_now - time_last) needs to be positive if we need to wait
  // 10^6 is for conversion to microseconds. Done before CLOCKS_PER_SEC to keep
  // accuracy	
  struct timespec now;
  clock_gettime(CLOCK_REALTIME, &now);
  wait = (now.tv_sec*BILLION + now.tv_nsec) - (spec_ops->wait_last_sent->tv_sec*BILLION + spec_ops->wait_last_sent->tv_nsec);
#if(DEBUG_OUTPUT)
fprintf(stdout, "UDP_STREAMER: %ld ns has passed since last send\n", wait);
#endif
if(wait < *(spec_ops->wait_nanoseconds)){
  //int mysleep = ((*(spec_ops->wait_nanoseconds)-wait)*1000000)/CLOCKS_PER_SEC;
#if(DEBUG_OUTPUT)
fprintf(stdout, "UDP_STREAMER: Sleeping %ld ys before sending packet\n", (*(spec_ops->wait_nanoseconds) - wait)/1000);
#endif	
usleep((*(spec_ops->wait_nanoseconds) - wait)/1000);
}
#ifdef ONLY_INCREMENT_TIMER
if(wait > *(spec_ops->wait_nanoseconds)){
#if(DEBUG_OUTPUT)
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
#endif // HAVE_RATELIMITER
err = sendto(spec_ops->fd, buf, spec_ops->buf_elem_size, 0, spec_ops->sin,spec_ops->sinsize);
#ifdef HAVE_RATELIMITER
//(spec_ops->wait_last_sent) = clock();
#endif


// Increment to the next sendable packet
*(spec_ops->cumul) += 1;
if(err < 0){
  perror("Send packet");
  // Set the cumul to -1 so we can inform other threads to quit 
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
    // We don't have the next packet
    pthread_cond_broadcast(spec_ops->signal);
  }
  // TODO: This a little bit of an extra unlock, but it'll do for now 
  pthread_mutex_unlock(spec_ops->cumlock);
  spec_ops->total_captured_bytes +=(unsigned int) err;
  spec_ops->total_captured_packets++;
}
i++;

// Sending the packets deemed more crucial so only releasing io mutex and signaling after
/ We've sent the package 
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
if(i%spec_ops->do_w_stuff_every == 0 && spec_ops->be->is_blocked(spec_ops->be) == 1)// || err == 0)
#else
if(i%spec_ops->do_w_stuff_every == 0)
#endif
  pthread_cond_signal(spec_ops->iosignal);
  // Release io thread to check on head
  pthread_mutex_unlock(spec_ops->headlock);
  }
*/
#if(DEBUG_OUTPUT)
fprintf(stdout, "UDP_STREAMER: Closing buffer thread\n");
#endif
//return sender_exit(spec_ops);
pthread_exit(NULL);
}

void* udp_rxring(void *streamo)
{
  int err = 0;
  //long i=0;
  long j = 0;
  //TODO: Get the timeout from main here.
  int timeout = 1000;
  struct streamer_entity *se =(struct streamer_entity*)streamo;
  struct udpopts *spec_ops = (struct udpopts *)se->opt;
  struct tpacket_hdr* hdr = spec_ops->opt->buffer + j*(spec_ops->opt->buf_elem_size); 
  struct pollfd pfd;
  int bufnum = 0;
  int *inc;

  spec_ops->total_captured_bytes = 0;
  spec_ops->total_captured_packets = 0;
#ifdef CHECK_OUT_OF_ORDER
  spec_ops->out_of_order = 0;
#endif
  spec_ops->incomplete = 0;
  spec_ops->dropped = 0;
  fprintf(stdout, "PKT OFFSET %lu tpacket_offset %lu\n", PKT_OFFSET, sizeof(struct tpacket_hdr));

  pfd.fd = spec_ops->fd;
  pfd.revents = 0;
  pfd.events = POLLIN|POLLRDNORM|POLLERR;
  D("Starting mmap polling");

  se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch, spec_ops->opt->cumul, bufnum);
  inc = se->be->get_inc(se->be);
  CHECK_AND_EXIT(se->be);

  while(spec_ops->running){
    if(!(hdr->tp_status  & TP_STATUS_USER)){
      //D("Polling pfd");
      err = poll(&pfd, 1, timeout);
      //CHECK_ERR_LTZ("Polled");
    }
    while(hdr->tp_status & TP_STATUS_USER){
      j++;

      if(hdr->tp_status & TP_STATUS_COPY){
	spec_ops->incomplete++;
	spec_ops->total_captured_packets++;
      }
      else if (hdr ->tp_status & TP_STATUS_LOSING){
	spec_ops->dropped++;
	spec_ops->total_captured_packets++;
      }
      else{
	spec_ops->total_captured_bytes += hdr->tp_len;
	spec_ops->total_captured_packets++;
	(*inc)++;
      }

      /* A buffer is ready for writing */
      if((j % spec_ops->opt->buf_num_elems) == 0){
	D("Buffo!");

	se->be->set_ready(se->be);
	pthread_mutex_lock(se->be->headlock);
	pthread_cond_signal(se->be->iosignal);
	pthread_mutex_unlock(se->be->headlock);

	se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch, spec_ops->opt->cumul, bufnum);
	CHECK_AND_EXIT(se->be);
	inc = se->be->get_inc(se->be);

	if(j==spec_ops->opt->buf_num_elems*(spec_ops->opt->n_threads)){
	  j=0;
	  bufnum =0;
	}
	else
	  bufnum++;

	/* cumul is tracking the n of files we've received */
	spec_ops->opt->cumul++;
      }
#ifdef SHOW_PACKET_METADATA
      fprintf(stdout, "Metadata for %ld packet: status: %lu, len: %u, snaplen: %u, MAC: %hd, net: %hd, sec %u, usec: %u\n", j, hdr->tp_status, hdr->tp_len, hdr->tp_snaplen, hdr->tp_mac, hdr->tp_net, hdr->tp_sec, hdr->tp_usec);
#endif

      hdr->tp_status = TP_STATUS_KERNEL;
      hdr = spec_ops->opt->buffer + j*(spec_ops->opt->buf_elem_size); 
    }
    //D("Packets handled");
    //fprintf(stdout, "i: %d, j: %d\n", i,j);
    hdr = spec_ops->opt->buffer + j*(spec_ops->opt->buf_elem_size); 
  }
  D("Exiting mmap polling");

  pthread_exit(NULL);
}
/*
 * Receiver for UDP-data
 */
void* udp_receiver(void *streamo)
{
  int err = 0;
  int i=0;
  int *inc;
  struct streamer_entity *se =(struct streamer_entity*)streamo;
  struct udpopts *spec_ops = (struct udpopts *)se->opt;
  spec_ops->total_captured_bytes = 0;
  spec_ops->total_captured_packets = 0;
#ifdef CHECK_OUT_OF_ORDER
  spec_ops->out_of_order = 0;
#endif
  spec_ops->incomplete = 0;
  spec_ops->dropped = 0;

  se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch, spec_ops->opt->cumul,0);
  CHECK_AND_EXIT(se->be);
  void * buf = se->be->simple_get_writebuf(se->be, &inc);

#if(DEBUG_OUTPUT)
  fprintf(stdout, "UDP_STREAMER: Starting stream capture\n");
#endif
  while(spec_ops->running){

    if(i == spec_ops->opt->buf_num_elems){
      D("Buffer filled, Getting another");

      /* Update cumul so packages are written to different files */
      spec_ops->opt->cumul += 1;

      /* Set old buffer ready and signal it to start writing */
      se->be->set_ready(se->be);
      pthread_mutex_lock(se->be->headlock);
      pthread_cond_signal(se->be->iosignal);
      pthread_mutex_unlock(se->be->headlock);

      /* Get a new buffer */
      se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch, spec_ops->opt->cumul,0);
      CHECK_AND_EXIT(se->be);
      buf = se->be->simple_get_writebuf(se->be, &inc);
      i=0;
    }
    err = recv(spec_ops->fd, buf, spec_ops->opt->buf_elem_size,0);
    if(err < 0){
      if(err == EINTR)
	fprintf(stdout, "UDP_STREAMER: Main thread has shutdown socket\n");
      else{
	perror("RECV error");
	fprintf(stderr, "UDP_STREAMER: Buf was at %lu\n", (long unsigned)buf);
      }
      spec_ops->running = 0;
      break;
    }
    /* Success! */
    else{
      i++;
      buf+=spec_ops->opt->buf_elem_size;
      (*inc)++;

      spec_ops->total_captured_bytes +=(unsigned int) err;
      spec_ops->total_captured_packets += 1;
      if(spec_ops->handle_packet != NULL)
	spec_ops->handle_packet(se,buf);
    }
  }
  //#if(DEBUG_OUTPUT)
  fprintf(stdout, "UDP_STREAMER: Closing streamer thread\n");
  //#endif
  //return sender_exit(spec_ops);
  pthread_exit(NULL);

}
void get_udp_stats(void *sp, void *stats){
  struct stats *stat = (struct stats * ) stats;
  struct udpopts *spec_ops = (struct udpopts*)sp;
  if(spec_ops->opt->optbits & USE_RX_RING)
    stat->total_packets += spec_ops->total_captured_packets;
  stat->total_bytes += spec_ops->total_captured_bytes;
  stat->incomplete += spec_ops->incomplete;
  stat->dropped += spec_ops->dropped;
}
int close_udp_streamer(void *opt_own, void *stats){
  struct udpopts *spec_ops = (struct udpopts *)opt_own;
  get_udp_stats(opt_own,  stats);

  //close(spec_ops->fd);
  //close(spec_ops->rp->fd);

#if(DEBUG_OUTPUT)
  fprintf(stdout, "UDP_STREAMER: Closed\n");
#endif
  //stats->packet_index = spec_ops->packet_index;
  //spec_ops->be->close(spec_ops->be, stats);
  //free(spec_ops->be);
  /* So if we're reading, just let the recorder end free the packet_index */
  //else
  if(!(spec_ops->opt->optbits & USE_RX_RING))
    free(spec_ops->sin);
  //free(spec_ops->headlock);
  //free(spec_ops->iosignal);
  free(spec_ops);
  return 0;
}
void udps_stop(struct streamer_entity *se){
  ((struct udpopts *)se->opt)->running = 0;
}
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
int udps_is_blocked(struct streamer_entity *se){
  return ((struct udpopts *)(se->opt))->is_blocked;
}
#endif
/*
   unsigned long udps_get_max_packets(struct streamer_entity *se){
   return ((struct opts*)(se->opt))->max_num_packets;
   }
   */
void udps_init_default(struct opt_s *opt, struct streamer_entity *se)
{
  se->init = setup_udp_socket;
  se->close = close_udp_streamer;
  se->get_stats = get_udp_stats;
  se->close_socket = udps_close_socket;
  //se->get_max_packets = udps_get_max_packets;
}

int udps_init_udp_receiver( struct opt_s *opt, struct streamer_entity *se)
{

  udps_init_default(opt,se);
  if(opt->optbits & USE_RX_RING)
    se->start = udp_rxring;
  else
    se->start = udp_receiver;
  se->stop = udps_stop;

  return se->init(opt, se);
}

int udps_init_udp_sender( struct opt_s *opt, struct streamer_entity *se)
{

  udps_init_default(opt,se);
  se->start = udp_sender;
  return se->init(opt, se);

}
