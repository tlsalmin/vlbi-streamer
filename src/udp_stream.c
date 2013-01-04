/*
 * udpstream.c -- UDP packet receiver and sender for vlbi-streamer
 *
 * Written by Tomi Salminen (tlsalmin@gmail.com)
 * Copyright 2012 Metsähovi Radio Observatory, Aalto University.
 * All rights reserved
 * This file is part of vlbi-streamer.
 *
 * vlbi-streamer is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * vlbi-streamer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with vlbi-streamer.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */
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
#include "config.h"
#include "streamer.h"
#include "udp_stream.h"
#include "resourcetree.h"
#include "timer.h"
#include "confighelper.h"
#include "common_filehandling.h"

#define WRONGSIZELIMITBEFOREEXIT 20
//#define FORCE_WRITE_TO_FILESIZE

extern FILE* logfile;

//#define SAUGMENTLOCK do{if(spec_ops->opt->optbits & (LIVE_SENDING | LIVE_RECEIVING)){pthread_spin_lock(spec_ops->opt->augmentlock);}}while(0)

//#define SAUGMENTUNLOCK do{if(spec_ops->opt->optbits & (LIVE_SENDING | LIVE_RECEIVING)){pthread_spin_unlock(spec_ops->opt->augmentlock);}}while(0)

#define FULL_COPY_ON_PEEK

#define UDPMON_SEQNUM_BYTES 8
//#define DUMMYSOCKET
#define BAUD_LIMITING
#define SLEEP_ON_BUFFERS_TO_LOAD
#define RATE_LIMITING_FROM_SOCKETSIZE
/* Using this until solved properly */
//#define UGLY_BUSYLOOP_ON_TIMER
/* Most of TPACKET-stuff is stolen from codemonkey blog */
/* http://codemonkeytips.blogspot.com/			*/
/// Offset of data from start of frame
#define PKT_OFFSET      (TPACKET_ALIGN(sizeof(struct tpacket_hdr)) + \
                         TPACKET_ALIGN(sizeof(struct sockaddr_ll)))
#define BIND_WITH_PF_PACKET
#define PLOTTABLE_SEND_DEBUG 0
//#define SHOW_PACKET_METADATA;

//#define UDPS_EXIT do {D("UDP_STREAMER: Closing sender thread. Left to send %lu, total sent: %lu",, st.packets_sent, spec_ops->total_captured_packets); if(se->be != NULL){set_free(spec_ops->opt->membranch, se->be->self);} spec_ops->running = 0;pthread_exit(NULL);}while(0)
#define UDPS_EXIT do {D("UDP_STREAMER: Closing sender thread. Left to send %lu, total sent: %lu",, st.packets_sent, spec_ops->total_captured_packets); if(se->be != NULL){set_free(spec_ops->opt->membranch, se->be->self);} spec_ops->opt->status = STATUS_STOPPED;pthread_exit(NULL);}while(0)

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
    D("Connecting to %s",, spec_ops->opt->hostname);
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
  if(!(spec_ops->opt->optbits & READMODE)){
    if(spec_ops->opt->hostname != NULL){
      spec_ops->sin_send = (struct sockaddr_in*) malloc(sizeof(struct sockaddr_in));
      memset(spec_ops->sin_send, 0, sizeof(struct sockaddr_in));   
      spec_ops->sin_send->sin_family = AF_INET;           
      spec_ops->sin_send->sin_port = htons(spec_ops->opt->port);    
      /* Were resending this stream at the same time */
      spec_ops->sin_send->sin_addr.s_addr = spec_ops->opt->serverip;
      spec_ops->sinsize = sizeof(struct sockaddr_in);
    }
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
#define MODE_FROM_OPTS -1
int udps_common_init_stuff(struct opt_s *opt, int mode, int* fd)
{
  int err,len,def,defcheck;

  if(mode == MODE_FROM_OPTS){
    D("Mode from opts");
    mode = opt->optbits;
  }
  //struct udpopts * spec_ops = se->opt;


  if(opt->device_name != NULL){
    //struct sockaddr_ll ll;
    struct ifreq ifr;
    //Get the interface index
    memset(&ifr, 0, sizeof(ifr));
    strcpy(ifr.ifr_name, opt->device_name);
    err = ioctl(*fd, SIOCGIFINDEX, &ifr);
    CHECK_ERR_LTZ("Interface index find");

    D("Binding to %s",, opt->device_name);
    err = setsockopt(*fd, SOL_SOCKET, SO_BINDTODEVICE, (void*)&ifr, sizeof(ifr));
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
  strcpy(ifr.ifr_name, opt->device_name);
  ifr.ifr_data = (void *)&hwconfig;

  err  = ioctl(*fd, SIOCSHWTSTAMP,&ifr);
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
  if(mode & READMODE){
    err = 0;
    D("Doing the double sndbuf-loop");
    def = opt->packet_size;
    while(err == 0){
      //D("RCVBUF size is %d",,def);
      def  = def << 1;
      err = setsockopt(*fd, SOL_SOCKET, SO_SNDBUF, &def, (socklen_t) len);
      if(err == 0){
	D("Trying SNDBUF size %d",, def);
      }
      err = getsockopt(*fd, SOL_SOCKET, SO_SNDBUF, &defcheck, (socklen_t * )&len);
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
    def = opt->packet_size;
    while(err == 0){
      //D("RCVBUF size is %d",,def);
      def  = def << 1;
      err = setsockopt(*fd, SOL_SOCKET, SO_RCVBUF, &def, (socklen_t) len);
      if(err == 0){
	D("Trying RCVBUF size %d",, def);
      }
      err = getsockopt(*fd, SOL_SOCKET, SO_RCVBUF, &defcheck, (socklen_t * )&len);
      if(defcheck != (def << 1)){
	D("Limit reached. Final size is %d Bytes",,defcheck);
	break;
      }
    }
  }

#ifdef SO_NO_CHECK
  if(mode & READMODE){
    const int sflag = 1;
    err = setsockopt(*fd, SOL_SOCKET, SO_NO_CHECK, &sflag, sizeof(sflag));
    CHECK_ERR("UDPCHECKSUM");
  }
#endif

#ifdef HAVE_LINUX_NET_TSTAMP_H
  //set hardware timestamping
  int req = 0;
  req |= SOF_TIMESTAMPING_SYS_HARDWARE;
  err = setsockopt(*fd, SOL_PACKET, PACKET_TIMESTAMP, (void *) &req, sizeof(req));
  CHECK_ERR("HWTIMESTAMP");
#endif
  return 0;
}

int setup_udp_socket(struct opt_s * opt, struct streamer_entity *se)
{
  int err;
  struct udpopts *spec_ops =(struct udpopts *) malloc(sizeof(struct udpopts));
  CHECK_ERR_NONNULL(spec_ops, "spec ops malloc");
  //spec_ops->running = 1;
  se->opt = (void*)spec_ops;

  spec_ops->opt = opt;

  /*
  if(!(spec_ops->opt->optbits & DATATYPE_UNKNOWN)){
    if(spec_ops->opt->optbits & DATATYPE_VDIF)
      spec_ops->calc_bufpos = calc_bufpos_vdif;
    else if(spec_ops->opt->optbits & DATATYPE_MARK5B)
      spec_ops->calc_bufpos = calc_bufpos_mark5b;
    else if(spec_ops->opt->optbits & DATATYPE_UDPMON)
      spec_ops->calc_bufpos = calc_bufpos_general;
    else{
      spec_ops->calc_bufpos = NULL;
      E("Unknown datatype not set, but no other either!");
    }
  }
  else
    spec_ops->calc_bufpos = NULL;
    */

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
    if(!(opt->optbits & READMODE) && opt->filename != NULL){
      spec_ops->fd_send = socket(AF_INET, SOCK_DGRAM, 0);
      if (spec_ops->fd_send < 0) {
	perror("socket for simusend");
	//INIT_ERROR
      }
      else{
	err = udps_common_init_stuff(spec_ops->opt, (spec_ops->opt->optbits|READMODE), &(spec_ops->fd_send));
	CHECK_ERR("Simusend init");
      }
    }
  }
  //if(!(spec_ops->optbits & READMODE))
  opt->socket = spec_ops->fd;


  if (spec_ops->fd < 0) {
    perror("socket");
    INIT_ERROR
  }
  err = udps_common_init_stuff(spec_ops->opt, MODE_FROM_OPTS, &(spec_ops->fd));
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
void * udp_sender(void *streamo){
  int err = 0;
  void* buf;
  //int i=0;
  long *inc, sentinc=0,packetcounter=0;
  //int max_buffers_in_use=0;
  //unsigned long cumulpeek;
  //unsigned long packetpeek;
  //int active_buffers;
  //struct fileholder* tempfh;
  /* If theres a wait_nanoseconds, it determines the amount of buffers	*/
  /* we have in use at any time						*/
  //int besindex;
  struct streamer_entity *se =(struct streamer_entity*)streamo;
  struct udpopts *spec_ops = (struct udpopts *)se->opt;
  struct sender_tracking st;

  init_sender_tracking(spec_ops->opt, &st);

  throttling_count(spec_ops->opt, &st);

  /* Init minimun sleeptime. On the test machine the minimum time 	*/
  /* Slept with nanosleep or usleep seems to be 55microseconds		*/
  /* This means we can sleep only sleep multiples of it and then	*/
  /* do the rest in a busyloop						*/
  long wait= 0;
  se->be = NULL;
  spec_ops->total_captured_bytes = 0;
  spec_ops->total_captured_packets = 0;
  spec_ops->out_of_order = 0;
  spec_ops->incomplete = 0;
  spec_ops->missing = 0;
  D("Wait between is %d here",, spec_ops->opt->wait_nanoseconds);

  //void * buf = se->be->simple_get_writebuf(se->be, &inc);
  D("Getting first loaded buffer for sender");
  
  jump_to_next_file(spec_ops->opt, se, &st);

  CHECK_AND_EXIT(se->be);

  /* Data won't be instantaneous so get min_sleep here! */
  unsigned long minsleep = get_min_sleeptime();
  LOG("Can sleep max %lu microseconds on average\n", minsleep);

  buf = se->be->simple_get_writebuf(se->be, &inc);

  D("Starting stream send");
  //i=0;
  GETTIME(spec_ops->opt->wait_last_sent);
  long packetpeek = get_n_packets(spec_ops->opt->fi);
  //while(st.files_sent <= spec_ops->opt->cumul && spec_ops->running){
  while(should_i_be_running(spec_ops->opt, &st) == 1){
    if(packetcounter == spec_ops->opt->buf_num_elems || (st.packets_sent - packetpeek  == 0))
    {
      err = jump_to_next_file(spec_ops->opt, se, &st);
      if(err == ALL_DONE)
	UDPS_EXIT;
      else if (err < 0){
	E("Error in getting buffer");
	UDPS_EXIT;
      }
      buf = se->be->simple_get_writebuf(se->be, &inc);
      packetpeek = get_n_packets(spec_ops->opt->fi);
      packetcounter = 0;
      sentinc = 0;
      //i=0;
    }
#ifdef HAVE_RATELIMITER
    if(spec_ops->opt->wait_nanoseconds > 0)
    {
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
      //fprintf(stdout,"%ld %ld \n",spec_ops->total_captured_packets, wait);
#else
      fprintf(stdout, "UDP_STREAMER: %ld ns has passed since last send\n", wait);
#endif
#endif
      //wait = spec_ops->opt->wait_nanoseconds - wait;
      ZEROTIME(st.req);

      //req.tv_nsec = spec_ops->opt->wait_nanoseconds - wait;
      SETNANOS(st.req,(spec_ops->opt->wait_nanoseconds-wait));
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
	//err = usleep(1);
#endif /*UGLY_BUSYLOOP_ON_TIMER */

	GETTIME(st.now);

#if(SEND_DEBUG)
#if(PLOTTABLE_SEND_DEBUG)
	fprintf(stdout, "%ld\n", nanodiff(&st.reference, &st.now));
#else
	fprintf(stdout, "UDP_STREAMER: Really slept %lu\n", nanodiff(&st.reference, &st.now));
#endif
#endif
	//COPYTIME(st.now,spec_ops->opt->wait_last_sent);

	nanoadd(&(spec_ops->opt->wait_last_sent), spec_ops->opt->wait_nanoseconds);	
      }
      else{
#if(SEND_DEBUG)
#if(PLOTTABLE_SEND_DEBUG)
	fprintf(stdout, "0 0\n");
#else
	fprintf(stdout, "Runaway timer! Resetting\n");
#endif
#endif
	COPYTIME(st.now,spec_ops->opt->wait_last_sent);
      }
    }

#endif //HAVE_RATELIMITER
#ifdef DUMMYSOCKET
    err = spec_ops->opt->packet_size;
#else
    err = sendto(spec_ops->fd, (buf+sentinc+spec_ops->opt->offset), (spec_ops->opt->packet_size-spec_ops->opt->offset), 0, spec_ops->sin,spec_ops->sinsize);
#endif


    // Increment to the next sendable packet
    if(err < 0){
      perror("Send packet");
      shutdown(spec_ops->fd, SHUT_RDWR);
      //pthread_exit(NULL);
      UDPS_EXIT;
      //TODO: How to handle error case? Either shut down all threads or keep on trying
      //pthread_exit(NULL);
      //break;
    }
    else if((unsigned)err != spec_ops->opt->packet_size){
      E("Sent only %d, when wanted to send %ld",, err, spec_ops->opt->packet_size);
    }
    else{
      st.packets_sent++;
      spec_ops->total_captured_bytes +=(unsigned int) err;
      spec_ops->total_captured_packets++;
      //buf += spec_ops->opt->packet_size;
      sentinc += spec_ops->opt->packet_size;
      packetcounter++;
    }
  }
  UDPS_EXIT;
}

/* RX-ring doesn't support simultaneous sending 	*/
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
  long *inc;

  struct rxring_request rxr;

  spec_ops->total_captured_bytes = 0;
  *spec_ops->opt->total_packets = 0;
  spec_ops->out_of_order = 0;
  spec_ops->incomplete = 0;
  spec_ops->missing = 0;
  D("PKT OFFSET %lu tpacket_offset %lu",, PKT_OFFSET, sizeof(struct tpacket_hdr));

  pfd.fd = spec_ops->fd;
  pfd.revents = 0;
  pfd.events = POLLIN|POLLRDNORM|POLLERR;
  D("Starting mmap polling");

  rxr.id = spec_ops->opt->cumul;
  rxr.bufnum = &bufnum;
  se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch, spec_ops->opt,(void*)&rxr, NULL);
  //inc = se->be->get_inc(se->be);
  se->be->simple_get_writebuf(se->be, &inc);
  CHECK_AND_EXIT(se->be);

  while(spec_ops->opt->status & STATUS_RUNNING){
    if(!(hdr->tp_status  & TP_STATUS_USER)){
      //D("Polling pfd");
      err = poll(&pfd, 1, timeout);
      SILENT_CHECK_ERRP_LTZ("Polled");
    }
    while(hdr->tp_status & TP_STATUS_USER){
      j++;

      if(hdr->tp_status & TP_STATUS_COPY){
	spec_ops->incomplete++;
	(*spec_ops->opt->total_packets)++;
      }
      else if (hdr ->tp_status & TP_STATUS_LOSING){
	spec_ops->missing++;
	(*spec_ops->opt->total_packets)++;
      }
      else{
	spec_ops->total_captured_bytes += hdr->tp_len;
	(*spec_ops->opt->total_packets)++;
	//(*inc)++;
	//TODO: Should we add s the extra?
	(*inc)+=hdr->tp_len;
      }

      /* A buffer is ready for writing */
      if((j % spec_ops->opt->buf_num_elems) == 0){
	D("Buffo!");

	se->be->set_ready(se->be);
	LOCK(se->be->headlock);
	pthread_cond_signal(se->be->iosignal);
	UNLOCK(se->be->headlock);
	/* Increment file counter! */
	//spec_ops->opt->n_files++;

	se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch, spec_ops->opt,&rxr, NULL);
	CHECK_AND_EXIT(se->be);
	//inc = se->be->get_inc(se->be);
	se->be->simple_get_writebuf(se->be, &inc);

	if(j==spec_ops->opt->buf_num_elems*(spec_ops->opt->n_threads)){
	  j=0;
	  bufnum =0;
	}
	else
	  bufnum++;

	/* cumul is tracking the n of files we've received */
	((*spec_ops->opt->cumul))++;
      }
#ifdef SHOW_PACKET_METADATA
      D("Metadata for %ld packet: status: %lu, len: %u, snaplen: %u, MAC: %hd, net: %hd, sec %u, usec: %u\n",, j, hdr->tp_status, hdr->tp_len, hdr->tp_snaplen, hdr->tp_mac, hdr->tp_net, hdr->tp_sec, hdr->tp_usec);
#endif

      hdr->tp_status = TP_STATUS_KERNEL;
      hdr = spec_ops->opt->buffer + j*(spec_ops->opt->packet_size); 
    }
    //D("Packets handled");
    //fprintf(stdout, "i: %d, j: %d\n", i,j);
    hdr = spec_ops->opt->buffer + j*(spec_ops->opt->packet_size); 
  }
  if(j > 0){
    (*spec_ops->opt->cumul)++;
    /* Since n_files starts from 0, we need to increment it here */
    /* What? legacy stuff i presume */
    (*spec_ops->opt->cumul)++;
  }
  D("Saved %lu files",, (*spec_ops->opt->cumul));
  D("Exiting mmap polling");
  //spec_ops->running = 0;
  spec_ops->opt->status = STATUS_STOPPED;

  pthread_exit(NULL);
}
inline void free_the_buf(struct buffer_entity * be){
  /* Set old buffer ready and signal it to start writing */
  be->set_ready(be);
  LOCK(be->headlock);
  pthread_cond_signal(be->iosignal);
  UNLOCK(be->headlock);
}
int jump_to_next_buf(struct streamer_entity* se, struct resq_info* resq){
  D("Jumping to next buffer!");
  struct udpopts* spec_ops = (struct udpopts*)se->opt;
  (*spec_ops->opt->cumul)++;
  /* Check if the buffer before still hasn't	*/
  /* gotten all packets				*/
  if(resq->before != NULL){
    D("Previous file still doesn't have all packets. Writing to disk and setting old packets as missing");
    //spec_ops->missing += spec_ops->opt->buf_num_elems - (*(resq->inc_before));
    /* Now heres a problem. We have to write the whole thing since	*/
    /* we don't know whats missing at this point. TODO: Fix when doing	*/
    /* fillpattern f4lz							*/
    spec_ops->missing += (FILESIZE-(*(resq->inc_before)))/spec_ops->opt->packet_size;
    //spec_ops->missing += 
    /* Write the to disk anyhow, so last packets aren't missed	*/
    //*(resq->inc_before) = spec_ops->opt->buf_num_elems;
    *(resq->inc_before) = FILESIZE;
    free_the_buf(resq->before);
    resq->bufstart_before = NULL;
    resq->before = NULL;
    resq->inc_before = NULL;
  }
  /* Check if we have all the packets for this file */
  //if(*(resq->inc) == spec_ops->opt->buf_num_elems){
  /* It looks silly, but inc was migrated to byte offset 		*/
  /* This setup lets use use arbitrary packet sizes with all buffers	*/
  //if(*(resq->inc)+spec_ops->opt->packet_size > FILESIZE)
  //if((resq->i) == spec_ops->opt->buf_num_elems)
  if((unsigned long)*(resq->inc) == (spec_ops->opt->buf_num_elems*(spec_ops->opt->packet_size)))
  {
    D("All packets for current file received OK. rsqinc: %ld, needed: %lu",, *resq->inc, spec_ops->opt->buf_num_elems*spec_ops->opt->packet_size);
#ifdef FORCE_WRITE_TO_FILESIZE
    *(resq->inc) = FILESIZE;
#endif
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
  resq->i=0;
  se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch, spec_ops->opt,spec_ops->opt->cumul, NULL);
  CHECK_AND_EXIT(se->be);
  resq->buf = se->be->simple_get_writebuf(se->be, &resq->inc);
  resq->bufstart = resq->buf;
  /* Set the next seqstart to += buf_num_elems	*/
  /* This way we can keep the buffers consistent	*/
  /* without holes from the resequencing logic	*/
  resq->seqstart_current += spec_ops->opt->buf_num_elems;

  return 0;
}
void*  calc_bufpos_general(void* header, struct streamer_entity* se, struct resq_info *resq){
  /* The seqnum is the first 64bits of the packet 	*/
  struct udpopts* spec_ops = (struct udpopts*)se->opt;

  long seqnum;
  switch (spec_ops->opt->optbits & LOCKER_DATATYPE){
    case DATATYPE_UDPMON:
      seqnum = getseq_udpmon(header);
      break;
    case DATATYPE_VDIF:
      seqnum = getseq_vdif(header, resq);
      break;
    case DATATYPE_MARK5BNET:
      seqnum = getseq_mark5b_net(header);
      break;
    case DATATYPE_MARK5B:
      //seqnum = getseq_mark5b(header);
      //TODO!
      /* Used is in trouble if hes sending plain mark5bs anyway */
      seqnum = resq->current_seq+1;
      break;
    default:
      E("Invalid datatype set!");
      return NULL;
  }

  if (resq->current_seq == INT64_MAX && resq->seqstart_current== INT64_MAX){
    resq->current_seq = resq->seqstart_current = seqnum;
    //(*(resq->inc))++;
    (*(resq->inc))+=spec_ops->opt->packet_size;
    resq->i++;
    resq->buf+= spec_ops->opt->packet_size;
    D("Got first packet with seqnum %ld",, seqnum);
    return header;
  }
  /*
  long seqnum = *((long*)header);
  seqnum = be64toh(seqnum);
  */

  //int err;
  //memcpy(&seqnum, header, UDPMON_SEQNUM_BYTES); 

  /* Preliminary case					*/
  /* Packet in order					*/
  if(seqnum == resq->current_seq+1){
    resq->current_seq++;

    //(*(resq->inc))++;
    (*(resq->inc))+=spec_ops->opt->packet_size;
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
    long diff_to_current = seqnum - (resq->current_seq);
    D("Current status: i: %d, cumul: %lu, current_seq %ld, seqnum: %ld inc: %ld,  diff_from_start %ld, diff_from_current %ld seqstart %ld",, resq->i, (*spec_ops->opt->cumul), resq->current_seq, seqnum, *resq->inc,  diff_from_start, diff_to_current, resq->seqstart_current);
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

	assert(resq->inc_before != NULL);

	//(*(resq->inc_before))++;
	(*(resq->inc_before))+= spec_ops->opt->packet_size;

	/* Copy to the old pos */
	resq->usebuf = resq->bufstart_before + (spec_ops->opt->buf_num_elems + diff_from_start)*((long)spec_ops->opt->packet_size);
	memcpy(resq->usebuf, resq->buf, spec_ops->opt->packet_size);

	//if(*(resq->inc_before) == spec_ops->opt->buf_num_elems){
	if(*(resq->inc_before) + spec_ops->opt->packet_size > FILESIZE)
	{
	  D("Buffer before is ready. Freeing it");
	  free_the_buf(resq->before);
	  resq->bufstart_before = NULL;
	  resq->before = NULL;
	  resq->inc_before = NULL;
	}
	return NULL;
      }
      else{
	D("Packet behind order, but inside this buffer.");
	//resq->current_seq = seqnum;
	//(*(resq->inc))++;
	(*(resq->inc))+=spec_ops->opt->packet_size;

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

	//(*(resq->inc))++;
	(*(resq->inc))+=spec_ops->opt->packet_size;
	
	/* Move diff up as thought we'd be receiving normally at the new position 	*/
	resq->current_seq = seqnum;
	/* Since i_from_next_seqstart counts also 0 , we need to add 1 here		*/
	resq->i+=i_from_next_seqstart+1;
	resq->buf = resq->usebuf + spec_ops->opt->packet_size;

	return NULL;
      }
      else{
	D("Packet ahead of time but in this buffer!");
	//(*(resq->inc))++;
	(*(resq->inc))+=spec_ops->opt->packet_size;
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

	/* Since diff_to_current is current_seqnum - seqnum and current_seqnum is for	*/
	/* the packet received before this, we need to add one here. 			*/
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

  spec_ops->wrongsizeerrors = 0;
  spec_ops->total_captured_bytes = 0;
  *spec_ops->opt->total_packets = 0;
  spec_ops->out_of_order = 0;
  spec_ops->incomplete = 0;
  spec_ops->missing = 0;

  se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch, spec_ops->opt,spec_ops->opt->cumul, NULL);
  CHECK_AND_EXIT(se->be);

  resq->buf = se->be->simple_get_writebuf(se->be, &resq->inc);
  /* IF we have packet resequencing	*/
  if(!(spec_ops->opt->optbits & DATATYPE_UNKNOWN))
  {
    resq->bufstart = resq->buf;
    /* Set up preliminaries to -1 so we know to	*/
    /* init this in the calcpos			*/
    resq->current_seq= INT64_MAX;
    resq->packets_per_second = -1;
    resq->starting_second = -1;
    resq->seqstart_current = INT64_MAX;

    resq->usebuf = NULL;

    /* First buffer so before is null 	*/
    resq->bufstart_before = NULL;
    resq->before = NULL;
  }

  LOG("UDP_STREAMER: Starting stream capture\n");
  while(spec_ops->opt->status & STATUS_RUNNING){

    //if(resq->i == spec_ops->opt->buf_num_elems)
    //if(*(resq->inc) + spec_ops->opt->packet_size > FILESIZE)
    if(resq->i == spec_ops->opt->buf_num_elems)
    {
      D("Buffer filled, Getting another");

      if(!(spec_ops->opt->optbits & DATATYPE_UNKNOWN)){
	D("Jumping to next buffer normally");
	err = jump_to_next_buf(se, resq);
	if(err < 0){
	  E("Error in jump to next");
	  //spec_ops->running = 0;
	  spec_ops->opt->status = STATUS_ERROR;
	  break;
	}
      }
      else{
	D("Datatype unknown!");
	resq->i=0;
	(*spec_ops->opt->cumul)++;
#ifdef FORCE_WRITE_TO_FILESIZE
	(*resq->inc) = FILESIZE;
#endif

	D("Freeing used buffer to write %lu bytes for file %lu",,*(resq->inc), *(spec_ops->opt->cumul)-1);
	free_the_buf(se->be);
	/* Get a new buffer */
	se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch,spec_ops->opt ,spec_ops->opt->cumul, NULL);
	CHECK_AND_EXIT(se->be);
	D("Got new free be. Grabbing buffer");
	resq->buf = se->be->simple_get_writebuf(se->be, &resq->inc);
      }
    }
    err = recv(spec_ops->fd, resq->buf, spec_ops->opt->packet_size,0);
    //err = spec_ops->opt->packet_size;
    if(err < 0){
      if(err == EINTR)
	LOG("UDP_STREAMER: Main thread has shutdown socket\n");
      else{
	perror("RECV error");
	E("Buf start: %lu, end: %lu",, (long unsigned)resq->buf, (long unsigned)(resq->buf+spec_ops->opt->packet_size*spec_ops->opt->buf_num_elems));
	fprintf(stderr, "UDP_STREAMER: Buf was at %lu\n", (long unsigned)resq->buf);
	if(!(spec_ops->opt->optbits & DATATYPE_UNKNOWN)){
	  E("Current status: i: %d, cumul: %lu, current_seq %ld,  inc: %ld,   seqstart %ld",, resq->i, (*spec_ops->opt->cumul), resq->current_seq,  *resq->inc,  resq->seqstart_current);
	}
      }
      //spec_ops->running = 0;
      udps_close_socket(se);
      spec_ops->opt->status = STATUS_ERROR;
      break;
    }
    else if((long unsigned)err != spec_ops->opt->packet_size){
      if(spec_ops->opt->status & STATUS_RUNNING){
	E("Received packet of size %d, when expected %lu",, err, spec_ops->opt->packet_size);
	spec_ops->incomplete++;
	spec_ops->wrongsizeerrors++;
	if(spec_ops->wrongsizeerrors > WRONGSIZELIMITBEFOREEXIT){
	  E("Too many wrong size packets received. Please adjust packet size correctly. Exiting");
	  udps_close_socket(se);
	  spec_ops->opt->status = STATUS_ERROR;
	  break;
	}
      }
    }
    /* Success! */
    else if(spec_ops->opt->status & STATUS_RUNNING){
      if(spec_ops->opt->hostname != NULL){
	int senderr = sendto(spec_ops->fd_send, resq->buf, spec_ops->opt->packet_size, 0, spec_ops->sin_send,spec_ops->sinsize);
	if(senderr <0 ){
	  perror("send error");
	  E("Send er");
	}
	else if((unsigned long)senderr != spec_ops->opt->packet_size)
	  E("Different size sent onward. NOT HANDLED");
      }
    assert(resq->i < spec_ops->opt->buf_num_elems);
      /* i has to keep on running, so we always change	*/
      /* the buffer at a correct spot			*/

      /* Check if we have a func for checking the	*/
      /* correct sequence from the header		*/
      if(!(spec_ops->opt->optbits & DATATYPE_UNKNOWN)){
	/* Calc the position we should have		*/
	if(spec_ops->opt->first_packet == NULL)
	{
	  err = init_header(&(spec_ops->opt->first_packet), spec_ops->opt);
	  if (err != 0)
	  {
	    E("First metadata malloc failed!");
	  }
	  else{
	    err = copy_metadata(resq->buf, spec_ops->opt->first_packet, spec_ops->opt);
	    if(err != 0)
	    {
	      E("First metadata copying failed!");
	    }
	    spec_ops->opt->resqut = resq;
	  }
	}
	calc_bufpos_general(resq->buf, se, resq);
      }
      else{
	resq->buf+=spec_ops->opt->packet_size;
	(*(resq->inc))+=spec_ops->opt->packet_size;
	resq->i++;

      }
      assert(*resq->inc <= FILESIZE);
      spec_ops->total_captured_bytes +=(unsigned int) err;
      *spec_ops->opt->total_packets += 1;
      if(spec_ops->opt->last_packet == *spec_ops->opt->total_packets){
	LOG("Captured %lu packets as specced. Exiting\n", spec_ops->opt->last_packet);
	spec_ops->opt->status = STATUS_FINISHED;
	break;
      }
    }
  }
  /* Release last used buffer */
  if(resq->before != NULL){
    //*(resq->inc_before) = spec_ops->opt->buf_num_elems;
    *(resq->inc_before) = FILESIZE;
    free_the_buf(resq->before);
  }
  if(*(resq->inc) == 0)
    se->be->cancel_writebuf(se->be);
  else{
    se->be->set_ready(se->be);
    (*spec_ops->opt->cumul)++;
  }
  LOCK(se->be->headlock);
  pthread_cond_signal(se->be->iosignal);
  UNLOCK(se->be->headlock);
  /* Set total captured packets as saveable. This should be changed to just */
  /* Use opts total packets anyway.. */
  //spec_ops->opt->total_packets = spec_ops->total_captured_packets;
  D("Saved %lu files and %lu packets",, (*spec_ops->opt->cumul), *spec_ops->opt->total_packets);
  LOG("UDP_STREAMER: Closing streamer thread\n");
  //spec_ops->running = 0;
  spec_ops->opt->status = STATUS_STOPPED;
  /* Main thread will free if we have a real datatype */
  if(spec_ops->opt->optbits & DATATYPE_UNKNOWN)
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
  stat->total_packets += *spec_ops->opt->total_packets;
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
  if(!(spec_ops->opt->optbits & READMODE) && spec_ops->opt->hostname != NULL){
    close(spec_ops->fd_send);
    free(spec_ops->sin_send);
  }
  close(spec_ops->fd);

  //close(spec_ops->fd);
  //close(spec_ops->rp->fd);

  //#if(DEBUG_OUTPUT)
  LOG("UDP_STREAMER: Closed\n");
  //#endif
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
  //((struct udpopts *)se->opt)->running = 0;
  ((struct opt_s*)((struct udpopts *)se->opt)->opt)->status = STATUS_STOPPED;
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
  (void)opt;
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
  se->stop = udps_stop;
  return se->init(opt, se);

}
