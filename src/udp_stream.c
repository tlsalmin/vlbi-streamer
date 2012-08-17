#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
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
#include <sys/mman.h> //for MMAP and poll
#include <sys/poll.h>

#include <pthread.h>
#include <assert.h>

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
#include <endian.h>

#include <net/if.h>
#include "streamer.h"
#include "udp_stream.h"
#include "resourcetree.h"
#include "timer.h"

#define FULL_COPY_ON_PEEK
#define MBITS_PER_DRIVE 500

#define UDPMON_SEQNUM_BYTES 8
//#define DUMMYSOCKET
#define BAUD_LIMITING
#define SLEEP_ON_BUFFERS_TO_LOAD
/* Using this until solved properly */
#define UGLY_BUSYLOOP_ON_TIMER
/* Most of TPACKET-stuff is stolen from codemonkey blog */
/* http://codemonkeytips.blogspot.com/			*/
/// Offset of data from start of frame
#define PKT_OFFSET      (TPACKET_ALIGN(sizeof(struct tpacket_hdr)) + \
                         TPACKET_ALIGN(sizeof(struct sockaddr_ll)))
#define BIND_WITH_PF_PACKET
#define PLOTTABLE_SEND_DEBUG 1
//#define SHOW_PACKET_METADATA;

struct sender_tracking{
  unsigned long files_loaded;
  unsigned long files_sent;
  unsigned long files_skipped;
  unsigned long packets_left_to_load;
  unsigned long packets_left_to_send;
  TIMERTYPE now;
#if(SEND_DEBUG)
  TIMERTYPE reference;
#endif
#ifdef UGLY_BUSYLOOP_ON_TIMER
  TIMERTYPE onenano;
#endif
  TIMERTYPE req;
};
#define UDPS_EXIT do {D("UDP_STREAMER: Closing sender thread. Left to send %lu, total sent: %lu",, st.packets_left_to_send, spec_ops->total_captured_packets); if(se->be != NULL){set_free(spec_ops->opt->membranch, se->be->self);} spec_ops->running = 0;pthread_exit(NULL);}while(0)

//Gatherer specific options
int phandler_sequence(struct streamer_entity * se, void * buffer){
  // TODO
  (void)se;
  (void)buffer;
  return 0;
}
void udps_close_socket(struct streamer_entity *se){
  D("Closing socket");
  int ret = shutdown(((struct udpopts*)se->opt)->fd, SHUT_RDWR);
  if(ret <0)
    perror("Socket shutdown");
}
int udps_bind_port(struct udpopts * spec_ops){
  int err=0;
  //prep port
  //struct sockaddr_in *addr = (struct sockaddr_in*) malloc(sizeof(struct sockaddr_in));
  spec_ops->sin = (struct sockaddr_in*) malloc(sizeof(struct sockaddr_in));
  CHECK_ERR_NONNULL(spec_ops->sin, "sin malloc");
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
    D("Binding to port %d",, spec_ops->opt->port);
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

  unsigned long total_mem_div_blocksize = (spec_ops->opt->packet_size*spec_ops->opt->buf_num_elems*spec_ops->opt->n_threads)/(spec_ops->opt->do_w_stuff_every);
  //req.tp_block_size = spec_ops->opt->packet_size*(spec_ops->opt->buf_num_elems)/4096;
  req.tp_block_size = spec_ops->opt->do_w_stuff_every;
  req.tp_frame_size = spec_ops->opt->packet_size;
  req.tp_frame_nr = spec_ops->opt->buf_num_elems*(spec_ops->opt->n_threads);
  //req.tp_block_nr = spec_ops->opt->n_threads;
  req.tp_block_nr = total_mem_div_blocksize;

  D("Block size: %d B Frame size: %d B Block nr: %d Frame nr: %d Max order:",,req.tp_block_size, req.tp_frame_size, req.tp_block_nr, req.tp_frame_nr);

  err = setsockopt(spec_ops->fd, SOL_PACKET, PACKET_RX_RING, (void *) &req, sizeof(req));
  CHECK_ERR("RX_RING SETSOCKOPT");

  //int flags = MAP_ANONYMOUS|MAP_SHARED;
  int flags = MAP_SHARED;
#if(HAVE_HUGEPAGES)
  if(spec_ops->opt->optbits & USE_HUGEPAGE)
    flags |= MAP_HUGETLB;
#endif
  assert((req.tp_block_size*req.tp_block_nr) % getpagesize() == 0);

  long maxmem = sysconf(_SC_AVPHYS_PAGES)*sysconf(_SC_PAGESIZE);
  unsigned long hog_memory =  (unsigned long)req.tp_block_size*((unsigned long)req.tp_block_nr);
  if((long unsigned)maxmem < hog_memory){
    E("Error in mem init. Memory available: %ld Memory wanted:  %lu",,maxmem, hog_memory);
    return -1;
  }

  spec_ops->opt->buffer = mmap(0, (unsigned long)req.tp_block_size*((unsigned long)req.tp_block_nr), PROT_READ|PROT_WRITE , flags, spec_ops->fd,0);
  if((long)spec_ops->opt->buffer <= 0){
    perror("Ring MMAP");
    return -1;
  }

  if(spec_ops->opt->device_name  !=NULL){
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
  }

  return 0;
}
int udps_common_init_stuff(struct streamer_entity *se)
{
  int err,len,def,defcheck;
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
  /*
#ifdef BAUD_LIMITING
  if(spec_ops->opt->optbits & WAIT_BETWEEN){
    struct ifreq ifr;
    //Get the interface index
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_ifru.ifru_ivalue = ((1000000000L)*spec_ops->opt->wait_nanoseconds)*8*spec_ops->opt->packet_size;
    err = ioctl(spec_ops->fd, SIOCSCANBAUDRATE, &ifr);
    CHECK_ERR("Baud Rater");
  }
#endif
*/
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
    err = 0;
    D("Doing the double rcvbuf-loop");
    def = spec_ops->opt->packet_size;
    while(err == 0){
      //D("RCVBUF size is %d",,def);
      def  = def << 1;
      err = setsockopt(spec_ops->fd, SOL_SOCKET, SO_SNDBUF, &def, (socklen_t) len);
      if(err == 0){
	D("Trying RCVBUF size %d",, def);
      }
      err = getsockopt(spec_ops->fd, SOL_SOCKET, SO_SNDBUF, &defcheck, (socklen_t * )&len);
      if(defcheck != (def << 1)){
	D("Limit reached. Final size is %d Bytes",,defcheck);
	break;
      }
    }
  }
  else{
    err=0;
    /*
    err = getsockopt(spec_ops->fd, SOL_SOCKET, SO_RCVBUF, &def, (socklen_t *) &len);
    CHECK_ERR("RCVBUF size");
    */
    D("Doing the double rcvbuf-loop");
    def = spec_ops->opt->packet_size;
    while(err == 0){
      //D("RCVBUF size is %d",,def);
      def  = def << 1;
      err = setsockopt(spec_ops->fd, SOL_SOCKET, SO_RCVBUF, &def, (socklen_t) len);
      if(err == 0){
	D("Trying RCVBUF size %d",, def);
      }
      err = getsockopt(spec_ops->fd, SOL_SOCKET, SO_RCVBUF, &defcheck, (socklen_t * )&len);
      if(defcheck != (def << 1)){
	D("Limit reached. Final size is %d Bytes",,defcheck);
	break;
      }
    }
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
  CHECK_ERR_NONNULL(spec_ops, "spec ops malloc");
  spec_ops->running = 1;
  se->opt = (void*)spec_ops;

  spec_ops->opt = opt;

  if(!(spec_ops->opt->optbits & DATATYPE_UNKNOWN)){
    /* TODO: Other than VDIF */
    if(spec_ops->opt->optbits & DATATYPE_VDIF)
      spec_ops->calc_bufpos = calc_bufpos_vdif;
    else if(spec_ops->opt->optbits & DATATYPE_MARK5B)
      spec_ops->calc_bufpos = calc_bufpos_mark5b;
    else if(spec_ops->opt->optbits & DATATYPE_UDPMON)
      spec_ops->calc_bufpos = calc_bufpos_udpmon;
    else{
      spec_ops->calc_bufpos = NULL;
      E("Unknown datatype not set, but no other either!");
    }
  }
  else
    spec_ops->calc_bufpos = NULL;

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
//inline int start_loading(struct opt_s * opt, unsigned long *packets_left, unsigned long *fileat, struct buffer_entity * be, unsigned long* files_skipped){
inline int start_loading(struct opt_s * opt, struct buffer_entity *be, struct sender_tracking *st)
{
  unsigned long nuf = MIN(st->packets_left_to_load, ((unsigned long)opt->buf_num_elems));
  while(opt->fileholders[st->files_loaded]  == -1 && st->files_loaded <= opt->cumul){
    D("Skipping a file, fileholder set to -1 for file %lu",, st->files_loaded);
    st->files_loaded++;
    st->files_skipped++;
    //spec_ops->files_sent++;
    /* If last file is missing, we might hit negative on left_to_send 	*/
    if(st->packets_left_to_send < (unsigned long)opt->buf_num_elems){
      st->packets_left_to_load=0;
      st->packets_left_to_send = 0;
    }
    else{
      st->packets_left_to_send -= opt->buf_num_elems;
      st->packets_left_to_load-=opt->buf_num_elems;
    }
    /* Special case where last file is missing */
    if(st->files_loaded == opt->cumul)
      return 0;
  }
  D("Requested a load start on file %lu",, st->files_loaded);
  if (be == NULL){
    be = get_free(opt->membranch, opt, st->files_loaded,0, NULL);
    CHECK_ERR_NONNULL(be, "Get loadable");
  /* Reacquiring just updates the file number we want */
  }
  else
    be->acquire((void*)be, opt, st->files_loaded,0);
  CHECK_AND_EXIT(be);
  D("Setting seqnum %lu to load %lu packets",,st->files_loaded, nuf);
  pthread_mutex_lock(be->headlock);
  //D("HUR");
  int * inc = be->get_inc(be);
  *inc = nuf;
  st->packets_left_to_load-=nuf;
  pthread_cond_signal(be->iosignal);
  pthread_mutex_unlock(be->headlock);
  D("Loading request complete for id %lu",, st->files_loaded);

  st->files_loaded++;
  return 0;
}
void init_sender_tracking(struct udpopts *spec_ops, struct sender_tracking *st){
  memset(st, 0,sizeof(struct sender_tracking));
  st->packets_left_to_send = st->packets_left_to_load = spec_ops->opt->total_packets;
#ifdef UGLY_BUSYLOOP_ON_TIMER
  //TIMERTYPE onenano;
  ZEROTIME(st->onenano);
  SETONE(st->onenano);
#endif
  ZEROTIME(st->req);
}
void * udp_sender(void *streamo){
  int err = 0;
  void* buf;
  int i=0;
  int *inc;
  int max_buffers_in_use=0;
  /* If theres a wait_nanoseconds, it determines the amount of buffers	*/
  /* we have in use at any time						*/
  //int besindex;
  struct streamer_entity *se =(struct streamer_entity*)streamo;
  struct udpopts *spec_ops = (struct udpopts *)se->opt;
  struct sender_tracking st;

  if(spec_ops->opt->wait_nanoseconds == 0){
    max_buffers_in_use = spec_ops->opt->n_threads;
    D("No wait set, so setting to use all available buffers");
  }
  else
  {
    long rate = (BILLION/((long)spec_ops->opt->wait_nanoseconds))*spec_ops->opt->packet_size*8;
    /* Add one as n loading for speed and one is being sent over the network */
    max_buffers_in_use = rate/(MBITS_PER_DRIVE*MILLION) + 1;
    D("rate as %d ns. Setting to use max %d buffers",, spec_ops->opt->wait_nanoseconds, max_buffers_in_use);
  }

  init_sender_tracking(spec_ops, &st);

  /* Init minimun sleeptime. On the test machine the minimum time 	*/
  /* Slept with nanosleep or usleep seems to be 55microseconds		*/
  /* This means we can sleep only sleep multiples of it and then	*/
  /* do the rest in a busyloop						*/
  long wait= 0;
  spec_ops->total_captured_bytes = 0;
  spec_ops->total_captured_packets = 0;
  spec_ops->out_of_order = 0;
  spec_ops->incomplete = 0;
  spec_ops->missing = 0;
  D("Wait between is %d here",, spec_ops->opt->wait_nanoseconds);

  /* This will run into trouble, when loading more packets than hard drives. The later packets can block the needed ones */
  //int loadup = MIN((unsigned int)spec_ops->opt->n_threads, spec_ops->opt->cumul);
  int loadup = MIN((unsigned int)max_buffers_in_use, spec_ops->opt->cumul);

  /* Check if theres empties right at the start */
  /* Added && for files might be skipped in start_loading */
  for(i=0;i<loadup && st.files_loaded < spec_ops->opt->cumul;i++){
    /* When running in sendmode, the buffers are first getted 	*/
    /* and then signalled to start loading packets from the hd 	*/
    /* A ready loaded buffer is getted by running get_loaded	*/
    /* With a matching sequence number				*/
    err = start_loading(spec_ops->opt, NULL, &st);
    CHECK_ERRP("Start loading");
  }

  /* Data won't be instantaneous so get min_sleep here! */
  unsigned long minsleep = get_min_sleeptime();
  D("Can sleep max %lu microseconds on average",, minsleep);

  //void * buf = se->be->simple_get_writebuf(se->be, &inc);
  D("Getting first loaded buffer for sender");
  while(spec_ops->opt->fileholders[st.files_sent] == -1 && st.files_sent <= spec_ops->opt->cumul)
    st.files_sent++;
  /* TODO: Handle dropped out rec points */
  if(st.files_sent < spec_ops->opt->cumul)
    se->be = get_loaded(spec_ops->opt->membranch, st.files_sent);
  else{
    UDPS_EXIT;
  }

  CHECK_AND_EXIT(se->be);
  buf = se->be->simple_get_writebuf(se->be, &inc);

  D("Starting stream send");
  i=0;
  //clock_gettime(CLOCK_REALTIME, &(spec_ops->opt->wait_last_sent));
  GETTIME(spec_ops->opt->wait_last_sent);
  while(st.files_sent <= spec_ops->opt->cumul){
    /* Need the OR here, since i wont hit buf_num_elems on the last file */
    if(i == spec_ops->opt->buf_num_elems || st.packets_left_to_send == 0){
      st.files_sent++;
      D("Buffer empty, Getting another: %lu",, st.files_sent);
      /* Check for missing file here so we can keep simplebuffer simple 	*/
      while(st.files_loaded < spec_ops->opt->cumul){
	if (spec_ops->opt->fileholders[st.files_loaded]  == -1){
	  D("Skipping a file, fileholder set to -1 for file %lu",, st.files_loaded);
	  st.files_loaded++;
	  st.files_skipped++;
	  //spec_ops->files_sent++;
	  /* If last file is missing, we might hit negative on left_to_send 	*/
	  if(st.packets_left_to_load < (unsigned long)spec_ops->opt->buf_num_elems){
	    st.packets_left_to_load = 0;
	  }
	  else
	    st.packets_left_to_load -= spec_ops->opt->buf_num_elems;
	}
	else
	  break;
      }
      //if((spec_ops->opt->cumul - files_loaded) > 0){
      /* Files loaded is incremented after we've gotten a load, so we can set	*/
      /* This as less than or equal						*/
      if(st.files_loaded < spec_ops->opt->cumul){
	D("Still files to be loaded. Loading %lu",, st.files_loaded);
	/* start_loading increments files_loaded */
	err = start_loading(spec_ops->opt, se->be, &st);
      }
      else{
	D("Loaded enough files as files_loaded %lu. Setting memorybuf to free",, st.files_loaded);
	set_free(spec_ops->opt->membranch, se->be->self);
      }

      while(st.files_sent < spec_ops->opt->cumul && spec_ops->opt->fileholders[st.files_sent] == -1)
	/* Skip it away now */
	st.files_sent++;
      if(st.files_sent < spec_ops->opt->cumul){
	D("Getting new loaded for file %lu",, st.files_sent);
	while(spec_ops->opt->fileholders[st.files_sent] == -1 && st.files_sent < spec_ops->opt->cumul)
	  st.files_sent++;
	if(st.files_sent < spec_ops->opt->cumul)
	  se->be = get_loaded(spec_ops->opt->membranch, st.files_sent);
	else{
	  UDPS_EXIT;
	}
	CHECK_AND_EXIT(se->be);
	buf = se->be->simple_get_writebuf(se->be, &inc);
	D("Got loaded file %lu to send.",, st.files_sent);
      }
      else{
	//E("Shouldn't be here since all packets have been sent!");
	D("All files sent! Time to wrap it up");
	//set_free(spec_ops->opt->membranch, se->be->self);
	break;
      }
      i=0;
    }
#ifdef HAVE_RATELIMITER
    if(spec_ops->opt->wait_nanoseconds > 0){
      //clock_gettime(CLOCK_REALTIME, &now);
      GETTIME(st.now);
#if(SEND_DEBUG)

      COPYTIME(st.now,st.reference);
      /*
	 reference.tv_sec = now.tv_sec;
	 reference.tv_nsec = now.tv_nsec;
	 */
#endif
      // waittime - (time_now - time_last) needs to be positive if we need to wait
      // 10^6 is for conversion to microseconds. Done before CLOCKS_PER_SEC to keep
      // accuracy	
      //wait = (now.tv_sec*BILLION + now.tv_nsec) - (spec_ops->opt->wait_last_sent.tv_sec*BILLION + spec_ops->opt->wait_last_sent.tv_nsec);
      wait = nanodiff(&(spec_ops->opt->wait_last_sent), &st.now);
#if(SEND_DEBUG)
#if(PLOTTABLE_SEND_DEBUG)
      fprintf(stdout, "%ld %ld ",spec_ops->total_captured_packets, wait);
#else
      fprintf(stdout, "UDP_STREAMER: %ld ns has passed since last send\n", wait);
#endif
#endif
      //wait = spec_ops->opt->wait_nanoseconds - wait;
      ZEROTIME(st.req);

      //req.tv_nsec = spec_ops->opt->wait_nanoseconds - wait;
      SETNANOS(st.req,spec_ops->opt->wait_nanoseconds-wait);
      if(GETNANOS(st.req) > 0){
	//int mysleep = ((*(spec_ops->wait_nanoseconds)-wait)*1000000)/CLOCKS_PER_SEC;
#if(SEND_DEBUG)
#if(PLOTTABLE_SEND_DEBUG)
	fprintf(stdout, "%ld ", GETNANOS(st.req));
#else
	fprintf(stdout, "UDP_STREAMER: Sleeping %ld ns before sending packet\n", GETNANOS(st.req));
#endif
#endif	
	//nanoadd(&now, wait);
	//req.tv_nsec = wait;
#ifdef UGLY_BUSYLOOP_ON_TIMER
	/* First sleep in minsleep sleeps to get rid of the bulk		*/
	while((unsigned long)GETNANOS(st.req) > minsleep){
	  SLEEP_NANOS(st.onenano);
	  SETNANOS(st.req,GETNANOS(st.req)-minsleep);
	}
	GETTIME(st.now);

	/* Then sleep in busyloop for finetuning				*/
	while(nanodiff(&(spec_ops->opt->wait_last_sent),&st.now) < spec_ops->opt->wait_nanoseconds){
	  /* This could be done in asm or by getting clock cycles but we don't  	*/
	  /* Control the NIC:s buffers anyway, so it doesn't really matter	*/
	  GETTIME(st.now);
	}
#else
	//err = nanosleep(&req,&rem);
	err = SLEEP_NANOS(st.req);
	GETTIME(st.now);
	//err = usleep(1);
#endif /*UGLY_BUSYLOOP_ON_TIMER */
#if(SEND_DEBUG)
#if(PLOTTABLE_SEND_DEBUG)
	fprintf(stdout, "%ld\n", nanodiff(&st.reference, &st.now));
#else
	fprintf(stdout, "UDP_STREAMER: Really slept %lu\n", nanodiff(&st.reference, &st.now));
#endif
#endif
      }
      else{
#if(SEND_DEBUG)
#if(PLOTTABLE_SEND_DEBUG)
	fprintf(stdout, "0 0\n");
#else
	fprintf(stdout, "Runaway timer! Resetting\n");
#endif
#endif
      }
      COPYTIME(st.now,spec_ops->opt->wait_last_sent);
    }

#endif //HAVE_RATELIMITER
#ifdef DUMMYSOCKET
    err = spec_ops->opt->packet_size;
#else
    err = sendto(spec_ops->fd, buf, spec_ops->opt->packet_size, 0, spec_ops->sin,spec_ops->sinsize);
#endif


    // Increment to the next sendable packet
    if(err < 0){
      perror("Send packet");
      shutdown(spec_ops->fd, SHUT_RDWR);
      pthread_exit(NULL);
      //TODO: How to handle error case? Either shut down all threads or keep on trying
      //pthread_exit(NULL);
      //break;
    }
    else{
      st.packets_left_to_send--;
      spec_ops->total_captured_bytes +=(unsigned int) err;
      spec_ops->total_captured_packets++;
      buf += spec_ops->opt->packet_size;
      i++;
    }
    }
  UDPS_EXIT;
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
  struct tpacket_hdr* hdr = spec_ops->opt->buffer + j*(spec_ops->opt->packet_size); 
  struct pollfd pfd;
  int bufnum = 0;
  int *inc;

  spec_ops->total_captured_bytes = 0;
  spec_ops->opt->total_packets = 0;
  spec_ops->out_of_order = 0;
  spec_ops->incomplete = 0;
  spec_ops->missing = 0;
  fprintf(stdout, "PKT OFFSET %lu tpacket_offset %lu\n", PKT_OFFSET, sizeof(struct tpacket_hdr));

  pfd.fd = spec_ops->fd;
  pfd.revents = 0;
  pfd.events = POLLIN|POLLRDNORM|POLLERR;
  D("Starting mmap polling");

  se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch, spec_ops->opt,spec_ops->opt->cumul, bufnum, NULL);
  inc = se->be->get_inc(se->be);
  CHECK_AND_EXIT(se->be);

  while(spec_ops->running){
    if(!(hdr->tp_status  & TP_STATUS_USER)){
      //D("Polling pfd");
      err = poll(&pfd, 1, timeout);
      SILENT_CHECK_ERRP_LTZ("Polled");
    }
    while(hdr->tp_status & TP_STATUS_USER){
      j++;

      if(hdr->tp_status & TP_STATUS_COPY){
	spec_ops->incomplete++;
	spec_ops->opt->total_packets++;
      }
      else if (hdr ->tp_status & TP_STATUS_LOSING){
	spec_ops->missing++;
	spec_ops->opt->total_packets++;
      }
      else{
	spec_ops->total_captured_bytes += hdr->tp_len;
	spec_ops->opt->total_packets++;
	(*inc)++;
      }

      /* A buffer is ready for writing */
      if((j % spec_ops->opt->buf_num_elems) == 0){
	D("Buffo!");

	se->be->set_ready(se->be);
	pthread_mutex_lock(se->be->headlock);
	pthread_cond_signal(se->be->iosignal);
	pthread_mutex_unlock(se->be->headlock);
	/* Increment file counter! */
	//spec_ops->opt->n_files++;

	se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch, spec_ops->opt,spec_ops->opt->cumul, bufnum, NULL);
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
      hdr = spec_ops->opt->buffer + j*(spec_ops->opt->packet_size); 
    }
    //D("Packets handled");
    //fprintf(stdout, "i: %d, j: %d\n", i,j);
    hdr = spec_ops->opt->buffer + j*(spec_ops->opt->packet_size); 
  }
  if(j > 0){
    spec_ops->opt->cumul++;
    /* Since n_files starts from 0, we need to increment it here */
    spec_ops->opt->cumul++;
  }
  D("Saved %lu files",, spec_ops->opt->cumul);
  D("Exiting mmap polling");
  spec_ops->running = 0;

  pthread_exit(NULL);
}
inline void free_the_buf(struct buffer_entity * be){
  /* Set old buffer ready and signal it to start writing */
  be->set_ready(be);
  pthread_mutex_lock(be->headlock);
  pthread_cond_signal(be->iosignal);
  pthread_mutex_unlock(be->headlock);
}
int jump_to_next_buf(struct streamer_entity* se, struct resq_info* resq){
  D("Jumping to next buffer!");
  struct udpopts* spec_ops = (struct udpopts*)se->opt;
  resq->i=0;
  spec_ops->opt->cumul++;
  /* Check if the buffer before still hasn't	*/
  /* gotten all packets				*/
  if(resq->before != NULL){
    D("Previous file still doesn't have all packets. Writing to disk and setting old packets as missing");
    spec_ops->missing += spec_ops->opt->buf_num_elems - (*(resq->inc_before));
    /* Write the to disk anyhow, so last packets aren't missed	*/
    *(resq->inc_before) = spec_ops->opt->buf_num_elems;
    free_the_buf(resq->before);
    resq->bufstart_before = NULL;
    resq->before = NULL;
  }
  /* Check if we have all the packets for this file */
  if(*(resq->inc) == spec_ops->opt->buf_num_elems){
    D("All packets for current file received OK");
    free_the_buf(se->be);
    /* First buffer so *before is null 	*/
    resq->bufstart_before = NULL;
    resq->before = NULL;
  }
  /* If not, then leave the old buf hanging 	*/
  else{
    D("Packets missing for current file. Leaving it to hang");
    resq->before = se->be;
    resq->bufstart_before = resq->bufstart;
    resq->inc_before = resq->inc;
  }
  se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch, spec_ops->opt,spec_ops->opt->cumul,0, NULL);
  CHECK_AND_EXIT(se->be);
  resq->buf = se->be->simple_get_writebuf(se->be, &resq->inc);
  resq->bufstart = resq->buf;
  /* Set the next seqstart to += buf_num_elems	*/
  /* This way we can keep the buffers consistent	*/
  /* without holes from the resequencing logic	*/
  resq->seqstart_current += spec_ops->opt->buf_num_elems;

  /*
  se->be = resq->after;
  resq->buf = resq->bufstart = resq->bufstart_after;
  resq->inc = resq->inc_after;
  */

  /*
  resq->after = (struct buffer_entity*)get_free(spec_ops->opt->membranch, spec_ops->opt,spec_ops->opt->cumul+1,0, NULL);
  CHECK_AND_EXIT(resq->after);
  resq->bufstart_after = resq->after->simple_get_writebuf(resq->after, &resq->inc_after);
  */

  return 0;
}
void * calc_bufpos_vdif(void* header, struct streamer_entity* se, struct resq_info *resq){
  /* TODO */
  (void)header;
  (void)se;
  (void)resq;
  return NULL;
}
void * calc_bufpos_mark5b(void* header, struct streamer_entity* se, struct resq_info *resq){
  /* TODO */
  (void)header;
  (void)se;
  (void)resq;
  return NULL;
}
void*  calc_bufpos_udpmon(void* header, struct streamer_entity* se, struct resq_info *resq){
  /* The seqnum is the first 64bits of the packet 	*/
  struct udpopts* spec_ops = (struct udpopts*)se->opt;
  long seqnum = *((long*)header);
  seqnum = be64toh(seqnum);

  //int err;
  //memcpy(&seqnum, header, UDPMON_SEQNUM_BYTES); 

  /* Preliminary case					*/
  if (resq->current_seq == INT64_MAX && resq->seqstart_current== INT64_MAX){
    resq->current_seq = resq->seqstart_current = seqnum;
    (*(resq->inc))++;
    resq->i++;
    resq->buf+= spec_ops->opt->packet_size;
    D("Got first packet with seqnum %ld",, seqnum);
    return header;
  }
  /* Packet in order					*/
  if(seqnum == resq->current_seq+1){
    resq->current_seq++;

    (*(resq->inc))++;
    resq->i++;
    resq->buf+= spec_ops->opt->packet_size;

    return header;
  }
  else if(seqnum == resq->current_seq){
    D("Packet with same seqnum %ld! Dropping..",, seqnum);
    return NULL;
  }
  else{
    long diff_from_start = seqnum - resq->seqstart_current;
    long diff_to_current = seqnum - (resq->current_seq+1);
    D("Current status: i: %d, cumul: %lu, current_seq %ld, seqnum: %ld inc: %d,  diff_from_start %ld, diff_from_current %ld seqstart %ld",, resq->i, spec_ops->opt->cumul, resq->current_seq, seqnum, *resq->inc,  diff_from_start, diff_to_current, resq->seqstart_current);
    if (diff_to_current < 0){
      D("Delayed packet. Returning correct pos. Seqnum: %ld old seqnum: %ld",, seqnum, resq->current_seq);
      if(diff_from_start < 0){
	D("Delayed beyond file. Writing to buffer_before");

	if(resq->bufstart_before == NULL){
	  E("Packet before first packet! Dropping!");
	  spec_ops->missing++;
	  return NULL;
	}
	else if (diff_from_start < -((long)spec_ops->opt->buf_num_elems)){
	  D("Packet beyond previous buffer. Dropping");
	  spec_ops->missing++;
	  return NULL;
	}

	(*(resq->inc_before))++;

	/* Copy to the old pos */
	resq->usebuf = resq->bufstart_before + (spec_ops->opt->buf_num_elems + diff_from_start)*((long)spec_ops->opt->packet_size);
	memcpy(resq->usebuf, resq->buf, spec_ops->opt->packet_size);

	if(*(resq->inc_before) == spec_ops->opt->buf_num_elems){
	  D("Buffer before is ready. Freeing it");
	  free_the_buf(resq->before);
	  resq->bufstart_before = NULL;
	  resq->before = NULL;
	}
	return NULL;
      }
      else{
	D("Packet behind order, but inside this buffer.");
	//resq->current_seq = seqnum;
	(*(resq->inc))++;

	resq->usebuf = (resq->bufstart + ((diff_from_start)*spec_ops->opt->packet_size));
	memcpy(resq->usebuf, resq->buf, spec_ops->opt->packet_size);

/*	
	D("Fugen! %lu",,(be64toh(*((unsigned long*)resq->bufstart +(seqnum - resq->seqstart_current)*((long)spec_ops->opt->packet_size)))));
	assert(be64toh(*((unsigned long*)resq->bufstart +(seqnum - resq->seqstart_current)*((long)spec_ops->opt->packet_size))) == (unsigned long)seqnum);
	*/
	//assert(resq->usebuf == temp);

	return NULL;
      }
      /*
      */
    }
    /* seqnum > current */
    else{
      D("Seqnum larger than current: %ld, old seqnum: %ld",, seqnum, resq->current_seq);
      if (diff_from_start >= spec_ops->opt->buf_num_elems){
	D("Packet ahead of time beyond this buffer!");
	if(diff_from_start >= spec_ops->opt->buf_num_elems*2){
	  D("Packet is way beyond buffer after. Dropping!");
	  spec_ops->missing++;
	  return NULL;
	}
	/* Need to jump to next buffer in this case */

	long i_from_next_seqstart = diff_from_start - (long)spec_ops->opt->buf_num_elems;
	/* Save pointer to packet		*/
	void* origbuf = resq->buf;

	int err = jump_to_next_buf(se, resq);
	if(err != 0){
	  E("Jump to next failed");
	  return NULL;
	}

	resq->usebuf = resq->bufstart + (i_from_next_seqstart)*spec_ops->opt->packet_size;
	memcpy(resq->usebuf, origbuf, spec_ops->opt->packet_size);

	(*(resq->inc))++;
	/* Move diff up as thought we'd be receiving normally at the new position 	*/
	resq->current_seq = seqnum;
	resq->i+=i_from_next_seqstart;
	resq->buf = resq->usebuf + spec_ops->opt->packet_size;

	return NULL;
      }
      else{
	D("Packet ahead of time but in this buffer!");
	(*(resq->inc))++;
	/* Jump to this position. This way we dont have to keep a bitmap of what we have etc.  */

	//resq->usebuf = resq->bufstart + (diff_from_start)*spec_ops->opt->packet_size;
	resq->usebuf = resq->bufstart + (((unsigned long)diff_from_start)*(spec_ops->opt->packet_size));
	memcpy(resq->usebuf, resq->buf, spec_ops->opt->packet_size);
	//void * temp = memcpy(resq->usebuf, resq->buf, spec_ops->opt->packet_size);

	/*
	D("memcpy copied stuff to %lu from start, when diff was %ld",, (long unsigned)((temp-resq->bufstart)/spec_ops->opt->packet_size), diff_from_start);
	D("Indabuff %lu",, be64toh(*((unsigned long*)temp)));
	D("Indabuff usebuf %lu",, be64toh(*((unsigned long*)resq->usebuf)));
	D("Indabuff shoulda %lu",, be64toh(*((unsigned long*)resq->bufstart + (((unsigned long)diff_from_start)*spec_ops->opt->packet_size))));

	*/
	//assert(be64toh(*((unsigned long*)resq->bufstart +((seqnum - resq->seqstart_current)*((unsigned long)spec_ops->opt->packet_size)))) == (unsigned long)seqnum);

	resq->i+= diff_to_current;
	resq->current_seq = seqnum;
	resq->buf = resq->usebuf + spec_ops->opt->packet_size;

	return NULL;
      }
    }
  }
  return NULL;
}
/*
 * Receiver for UDP-data
 */

void* udp_receiver(void *streamo)
{
  int err = 0;
  //int i=0;
  //void *buf;

  struct resq_info* resq = (struct resq_info*)malloc(sizeof(struct resq_info));
  memset(resq, 0, sizeof(struct resq_info));

  struct streamer_entity *se =(struct streamer_entity*)streamo;
  struct udpopts *spec_ops = (struct udpopts *)se->opt;

  spec_ops->total_captured_bytes = 0;
  spec_ops->opt->total_packets = 0;
  spec_ops->out_of_order = 0;
  spec_ops->incomplete = 0;
  spec_ops->missing = 0;

  se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch, spec_ops->opt,spec_ops->opt->cumul,0, NULL);
  CHECK_AND_EXIT(se->be);

  /* IF we have packet resequencing	*/
  if(spec_ops->calc_bufpos == NULL)
    resq->buf = se->be->simple_get_writebuf(se->be, &resq->inc);
  else{
    resq->buf = se->be->simple_get_writebuf(se->be, &resq->inc);
    resq->bufstart = resq->buf;

    /* Set up preliminaries to -1 so we know to	*/
    /* init this in the calcpos			*/
    resq->current_seq= INT64_MAX;
    resq->packets_per_second = -1;
    resq->current_second = -1;
    resq->seqstart_current = INT64_MAX;

    resq->usebuf = NULL;

    /* First buffer so before is null 	*/
    resq->bufstart_before = NULL;
    resq->before = NULL;
  }

#if(DEBUG_OUTPUT)
  fprintf(stdout, "UDP_STREAMER: Starting stream capture\n");
#endif
  while(spec_ops->running){

    if(resq->i == spec_ops->opt->buf_num_elems){
      D("Buffer filled, Getting another");


      if(spec_ops->calc_bufpos != NULL){
	D("Jumping to next buffer normally");
	err = jump_to_next_buf(se, resq);
	if(err < 0){
	  E("Error in jump to next");
	  spec_ops->running = 0;
	  break;
	}
      }
      else{
	resq->i=0;
	spec_ops->opt->cumul++;

	free_the_buf(se->be);

	/* Get a new buffer */
	se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch,spec_ops->opt ,spec_ops->opt->cumul,0, NULL);
	CHECK_AND_EXIT(se->be);
	resq->buf = se->be->simple_get_writebuf(se->be, &resq->inc);
      }
    }
    err = recv(spec_ops->fd, resq->buf, spec_ops->opt->packet_size,0);
    if(err < 0){
      if(err == EINTR)
	fprintf(stdout, "UDP_STREAMER: Main thread has shutdown socket\n");
      else{
	perror("RECV error");
	fprintf(stderr, "UDP_STREAMER: Buf was at %lu\n", (long unsigned)resq->buf);
      }
      spec_ops->running = 0;
      break;
    }
    else if((long unsigned)err != spec_ops->opt->packet_size){
      if(spec_ops->running == 1){
	E("Received packet of size %d, when expected %lu",, err, spec_ops->opt->packet_size);
	spec_ops->incomplete++;
      }
    }
    /* Success! */
    else if(spec_ops->running==1){
      /* i has to keep on running, so we always change	*/
      /* the buffer at a correct spot			*/

      /* Check if we have a func for checking the	*/
      /* correct sequence from the header		*/
      if(spec_ops->calc_bufpos != NULL){
	/* Calc the position we should have		*/
	//resq->usebuf = spec_ops->calc_bufpos(resq->buf, se, resq);
	spec_ops->calc_bufpos(resq->buf, se, resq);
	/*
	if(resq->usebuf == NULL){
	  D("Packet too much out of sequence!");
	  spec_ops->missing++;
	}
	else if (resq->usebuf == resq->buf){
	  // Packet in order! 
	  //resq->buf+=spec_ops->opt->packet_size;
	  //(*inc)++;
	}
	else{
	// Packet out of order. Memcopying to place 
	  memcpy(resq->usebuf, resq->buf, spec_ops->opt->packet_size);
	}
	*/
      }
      else{
	/* Ignore datatype and just be happy	*/
	resq->buf+=spec_ops->opt->packet_size;
	(*(resq->inc))++;
	resq->i++;

      }
      spec_ops->total_captured_bytes +=(unsigned int) err;
      spec_ops->opt->total_packets += 1;
    }
  }
  /* Release last used buffer */
  if(resq->before != NULL){
    *(resq->inc_before) = spec_ops->opt->buf_num_elems;
    free_the_buf(resq->before);
  }
  if(*(resq->inc) == 0)
    se->be->cancel_writebuf(se->be);
  else{
    se->be->set_ready(se->be);
    spec_ops->opt->cumul++;
  }
  pthread_mutex_lock(se->be->headlock);
  pthread_cond_signal(se->be->iosignal);
  pthread_mutex_unlock(se->be->headlock);
  /* Set total captured packets as saveable. This should be changed to just */
  /* Use opts total packets anyway.. */
  //spec_ops->opt->total_packets = spec_ops->total_captured_packets;
  D("Saved %lu files and %lu packets",, spec_ops->opt->cumul, spec_ops->opt->total_packets);
  fprintf(stdout, "UDP_STREAMER: Closing streamer thread\n");
  spec_ops->running = 0;
  free(resq);
  pthread_exit(NULL);

}
/*
   unsigned long udps_get_fileprogress(struct udpopts* spec_ops){
   if(spec_ops->opt->optbits & READMODE)
   return spec_ops->files_sent;
   else
   return spec_ops->opt->cumul;
   }
   */
void get_udp_stats(void *sp, void *stats){
  struct stats *stat = (struct stats * ) stats;
  struct udpopts *spec_ops = (struct udpopts*)sp;
  //if(spec_ops->opt->optbits & USE_RX_RING)
  stat->total_packets += spec_ops->opt->total_packets;
  stat->total_bytes += spec_ops->total_captured_bytes;
  stat->incomplete += spec_ops->incomplete;
  stat->dropped += spec_ops->missing;
  //stat->files_exchanged = udps_get_fileprogress(spec_ops);
}
int close_udp_streamer(void *opt_own, void *stats){
  struct udpopts *spec_ops = (struct udpopts *)opt_own;
  int err;
  get_udp_stats(opt_own,  stats);
  if(!(spec_ops->opt->optbits & READMODE)){
    err = set_from_root(spec_ops->opt, NULL, 0,1);
    CHECK_ERR("update_cfg");
    err = write_cfgs_to_disks(spec_ops->opt);
    CHECK_ERR("write_cfg");
  }
  close(spec_ops->fd);

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
  D("Stopping loop");
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
int udps_is_running(struct streamer_entity *se){
  return ((struct udpopts*)se->opt)->running;
}
void udps_init_default(struct opt_s *opt, struct streamer_entity *se)
{
  (void)opt;
  se->init = setup_udp_socket;
  se->close = close_udp_streamer;
  se->get_stats = get_udp_stats;
  se->close_socket = udps_close_socket;
  se->is_running = udps_is_running;
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
