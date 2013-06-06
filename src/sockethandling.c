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
#include <sys/mman.h> 	//for MMAP and poll
#include <linux/mman.h> //for MMAP and poll
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
#include "resourcetree.h"
#include "confighelper.h"
#include "common_filehandling.h"
#include "sockethandling.h"
int bind_port(struct addrinfo* si, int fd, int readmode, int do_connect){
  int err=0;
  struct addrinfo *p;

  if(readmode == 0)
  {
    /* TODO: this needs to be done earlier */
    for(p = si; p != NULL; p = p->ai_next)
    {
      err = bind(fd, p->ai_addr, p->ai_addrlen);
      if(err != 0)
      {
	E("bind socket");
	//close(sockfd);
	continue;
      }
      break;
    }
    if(err != 0){
      E("Cant bind");
      return -1;
    }
  }
  else if(do_connect == 1)
  {
    for(p = si; p != NULL; p = p->ai_next)
    {
      err = connect(fd, p->ai_addr, p->ai_addrlen);
      if(err != 0)
      {
	E("connect socket");
	//close(sockfd);
	continue;
      }
      break;
    }
    if(err != 0){
      E("Cant connect");
      return -1;
    }
  }

  return 0;
}
int create_socket(int *fd, char * port, struct addrinfo ** servinfo, char * hostname, int socktype, struct addrinfo ** used, uint64_t optbits, char* device_name)
{
  int err;
  struct addrinfo hints, *p;
  memset(&hints, 0, sizeof(struct addrinfo));
  /* Great ipv6 guide http://beej.us/guide/bgnet/					*/
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = socktype;
  hints.ai_flags = AI_PASSIVE;
  if(hostname == NULL){
    if(device_name != NULL){
      hostname = device_name;
      D("Creating socket to localhost port %s bound to interface %s",, port, hostname);
    }
    else
      D("Creating socket to localhost port %s",, port);
  }
  else
    D("Creating socket to %s port %s",, hostname, port);
  /* Port as integer is legacy from before I saw the light from Beej network guide	*/
  err = getaddrinfo(hostname, port, &hints, servinfo);
  if(err != 0){
    E("Error in getting address info %s",, gai_strerror(err));
    return -1;
  }
  err = -1;
  *fd = -1;
  if(hostname != NULL && port != NULL)
    D("Trying to connect socket to %s:%s",, hostname, port);
  for(p = *servinfo; p != NULL; p = p->ai_next)
  {
    if((*fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0)
    {
      E("Cant create socket to %s. Trying next",, p->ai_canonname);
      continue;
    }
    if(optbits & SO_REUSEIT)
    {
      int yes = 1;
      err = setsockopt(*fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
      CHECK_ERR("Reuseaddr");
    }
    if(optbits & READMODE)
    {
      if(optbits & CONNECT_BEFORE_SENDING)
      {
	D("Also connecting socket");
	if(connect(*fd, p->ai_addr, p->ai_addrlen) < 0)
	{
	  E("connect socket");
	  close(*fd);
	  continue;
	}
      }
    }
    else
    {
      if(bind(*fd, p->ai_addr, p->ai_addrlen) != 0)
      {
	close(*fd);
	*fd = -1;
	E("bind socket");
	continue;
      }
      D("Socket bound");
    }
    if(used != NULL)
      *used = p;
    D("Got socket!");
    err = 0;
    break;
  }
  if(err != 0 || *fd < 0){
    E("Couldn't get socket at all. Exiting as failed");
    return -1;
  }
  return 0;
}
int socket_common_init_stuff(struct opt_s *opt, int mode, int* fd)
{
  int err,len,def,defcheck;

  if(mode == MODE_FROM_OPTS){
    D("Mode from opts");
    mode = opt->optbits;
  }

  /*
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
void close_socket(struct streamer_entity *se){
  struct socketopts *spec_ops = se->opt;
  if(spec_ops->fd == 0)
  {
    D("Wont shut down already shutdown fd");
  }
  else
  {
    int ret;
    if(spec_ops->opt->optbits & READMODE){
      LOG("Closing socket on send of %s to %s\n", spec_ops->opt->filename, spec_ops->opt->hostname);
    }
    else{
      LOG("Closing socket on receive %s\n", spec_ops->opt->filename);
    }
    //if(spec_ops->opt->optbits | (CAPTURE_W_TCPSTREAM|CAPTURE_W_TCPSPLICE))
    if(!(spec_ops->opt->optbits & READMODE))
    {
      ret = shutdown(spec_ops->fd, SHUT_RD);
      if(ret != 0)
	D("Shutdown gave non-zero return");
    }
    else{
      ret = shutdown(spec_ops->fd, SHUT_WR);
      if(ret != 0)
	D("Shutdown gave non-zero return");
    }
    ret = close(spec_ops->fd);
    if(ret <0){
      E("close return something not ok");
    }
    spec_ops->fd = 0;
  }
}
void free_the_buf(struct buffer_entity * be){
  /* Set old buffer ready and signal it to start writing */
  be->set_ready_and_signal(be,0);
}
int close_streamer_opts(struct streamer_entity *se, void *stats){
  D("Closing udp-streamer");
  struct socketopts *spec_ops = (struct socketopts *)se->opt;
  int err;
  se->get_stats(se->opt, stats);
  D("Got stats");
  if(!(spec_ops->opt->optbits & READMODE)){
    D("setting cfg");
    err = set_from_root(spec_ops->opt, NULL, 0,1);
    CHECK_ERR("update_cfg");
    err = write_cfgs_to_disks(spec_ops->opt);
    CHECK_ERR("write_cfg");
  } 
  if(!(spec_ops->opt->optbits & READMODE) && spec_ops->opt->hostname != NULL){
    close(spec_ops->fd_send);
    free(spec_ops->sin_send);
  }
  if(spec_ops->servinfo != NULL)
    freeaddrinfo(spec_ops->servinfo);
  if(spec_ops->servinfo_simusend != NULL)
    freeaddrinfo(spec_ops->servinfo_simusend);
  LOG("UDP_STREAMER: Closed\n");
  /*
     if(spec_ops->sin != NULL)
     free(spec_ops->sin);
     */

  /*
     if(!(spec_ops->opt->optbits & USE_RX_RING))
     free(spec_ops->sin);
     */
  free(spec_ops);
  D("Returning");
  return 0;
}
void stop_streamer(struct streamer_entity *se){
  D("Stopping loop");
  struct socketopts* spec_ops = (struct socketopts*)se->opt;
  set_status_for_opt(spec_ops->opt, STATUS_STOPPED);
  close_socket(se);
}
void reset_udpopts_stats(struct socketopts *spec_ops)
{
  spec_ops->wrongsizeerrors = 0;
  spec_ops->total_transacted_bytes = 0;
  spec_ops->opt->total_packets = 0;
  spec_ops->out_of_order = 0;
  spec_ops->incomplete = 0;
  spec_ops->missing = 0;
}
/* Stolen from Beejs network guide */
// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in*)sa)->sin_addr);
  }
  return &(((struct sockaddr_in6*)sa)->sin6_addr);
}
int udps_wait_function(struct sender_tracking *st, struct opt_s* opt)
{
  long wait;
#if(PREEMPTKERNEL)
  int err;
#endif
#ifdef HAVE_RATELIMITER
  if(opt->wait_nanoseconds > 0)
  {
    //clock_gettime(CLOCK_REALTIME, &now);
    GETTIME(st->now);
#if(SEND_DEBUG)
    COPYTIME(st->now,st->reference);
#endif
    wait = nanodiff(&(opt->wait_last_sent), &st->now);
#if(SEND_DEBUG)
#if(PLOTTABLE_SEND_DEBUG)
    //fprintf(st->out,"%ld %ld \n",spec_ops->total_captured_packets, wait);
#else
    fprintf(st->out, "UDP_STREAMER: %ld ns has passed since last->send\n", wait);
#endif
#endif
    ZEROTIME(st->req);
    SETNANOS(st->req,(opt->wait_nanoseconds-wait));
    if(GETNANOS(st->req) > 0){
#if(SEND_DEBUG)
#if(PLOTTABLE_SEND_DEBUG)
      fprintf(st->out, "%ld ", GETNANOS(st->req));
#else
      fprintf(st->out, "UDP_STREAMER: Sleeping %ld ns before sending packet\n", GETNANOS(st->req));
#endif
#endif	
#if!(PREEMPTKERNEL)
      /* First->sleep in minsleep sleeps to get rid of the bulk		*/
      while((unsigned long)GETNANOS(st->req) > st->minsleep){
	SLEEP_NANOS(st->onenano);
	SETNANOS(st->req,GETNANOS(st->req)-st->minsleep);
      }
      GETTIME(st->now);

      while(nanodiff(&(opt->wait_last_sent),&st->now) < opt->wait_nanoseconds){
	GETTIME(st->now);
      }
#else
      err = SLEEP_NANOS(st->req);
      if(err != 0){
	E("cant sleep");
	return -1;
      }

      GETTIME(st->now);
#endif /*PREEMPTKERNEL */

#if(SEND_DEBUG)
#if(PLOTTABLE_SEND_DEBUG)
      fprintf(st->out, "%ld\n", nanodiff(&st->reference, &st->now));
#else
      fprintf(st->out, "UDP_STREAMER: Really slept %lu\n", nanodiff(&st->reference, &st->now));
#endif
#endif
      nanoadd(&(opt->wait_last_sent), opt->wait_nanoseconds);	
    }
    else{
#if(SEND_DEBUG)
#if(PLOTTABLE_SEND_DEBUG)
      fprintf(st->out, "0 0\n");
#else
      fprintf(st->out, "Runaway timer! Resetting\n");
#endif
#endif
      COPYTIME(st->now,opt->wait_last_sent);
    }
  }
#endif //HAVE_RATELIMITER
  return 0;
}
void bboundary_bytenum(struct streamer_entity* se, struct sender_tracking *st, unsigned long **counter)
{
  struct socketopts *spec_ops = se->opt;
  *counter = &st->packetcounter;
  **counter = MIN(st->total_bytes_to_send-spec_ops->total_transacted_bytes, spec_ops->opt->buf_num_elems*spec_ops->opt->packet_size);
  D("Packetboundary called for %s. Next boundary is %ld bytes",,spec_ops->opt->filename,  **counter);
}
void bboundary_packetnum(struct streamer_entity* se, struct sender_tracking *st, unsigned long **counter)
{
  struct socketopts *spec_ops = se->opt;
  *counter = &st->packetcounter;
  **counter = MIN(st->n_packets_probed-st->packets_sent, spec_ops->opt->buf_num_elems);
  D("Packetboundary called for %s. Next boundary is %ld packets",,spec_ops->opt->filename,  **counter);
}
int generic_sendloop(struct streamer_entity * se, int do_wait, int(*sendcmd)(struct streamer_entity*, struct sender_tracking*), void(*buffer_boundary)(struct streamer_entity*, struct sender_tracking*, unsigned long **))
{
  int err = 0;

  struct socketopts *spec_ops = (struct socketopts *)se->opt;
  struct sender_tracking st;
  unsigned long * counter;
  init_sender_tracking(spec_ops->opt, &st);
  ///if(do_wait == 1)
  throttling_count(spec_ops->opt, &st);
  se->be = NULL;

  spec_ops->total_transacted_bytes = 0;
  //spec_ops->total_captured_packets = 0;
  spec_ops->out_of_order = 0;
  spec_ops->incomplete = 0;
  spec_ops->missing = 0;
  D("Getting first loaded buffer for sender");

  spec_ops->inc = &st.inc;

  /* Data won't be instantaneous so get min_sleep here! */
  if(do_wait==1){
    unsigned long minsleep = get_min_sleeptime();
    LOG("Can sleep max %lu microseconds on average\n", minsleep);
#if!(PREEMPTKERNEL)
    st.minsleep = minsleep;
#endif
  }

  /*
     jump_to_next_file(spec_ops->opt, se, &st);
     CHECK_AND_EXIT(se->be);

     st.buf = se->be->simple_get_writebuf(se->be, NULL);

     D("Starting stream send");
     */


  LOG("GENERIC_SENDER: Starting stream send\n");
  if(do_wait == 1)
    GETTIME(spec_ops->opt->wait_last_sent);
  while(should_i_be_running(spec_ops->opt, &st) == 1){
    err = jump_to_next_file(spec_ops->opt, se, &st);
    if(err == ALL_DONE){
      D("Jump to next file returned all done");
      break;
    }
    else if (err < 0){
      E("Error in getting buffer");
      UDPS_EXIT_ERROR;
    }
    CHECK_AND_EXIT(se->be);
    se->be->simple_get_writebuf(se->be, NULL);
    *(spec_ops->inc) = 0;

    buffer_boundary(se,&st,&counter);

    while(*counter > 0)
    {
      if(do_wait==1)
	udps_wait_function(&st, spec_ops->opt);
      err = sendcmd(se, &st);
      if(err != 0){
	E("Error in sendcmd");
	UDPS_EXIT_ERROR;
      }
    }
  }
  UDPS_EXIT;
}
/* TODO: A generic function would be nice, but needs dev time	*/
/*
   int generic_recvloop(struct streamer_entity * se, int(*recvcmd)(struct streamer_entity*), unsigned long counter_up_to, struct resq_info, * resq,int (*buffer_switch_function)(struct streamer_entity *))
   {
   long err=0;
   struct socketopts * spec_ops = (struct socketopts*)se->opt;

   se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch, spec_ops->opt,&(spec_ops->opt->cumul), NULL,1);
   CHECK_AND_EXIT(se->be);
   se->be->simple_get_writebuf(se->be, spec_ops->inc);
   LOG("Starting stream capture for %s\n",, spec_ops->opt->filename);

   while(get_status_from_opt(spec_ops->opt) & STATUS_RUNNING)
   {
   while((*spec_ops->inc) < counter_up_to){
   err = recvcmd(se);
   if(err != 0){
   if(err < 0)
   E("Loop for %s ended in error",, spec_ops->opt->filename);
   else
   D("Finishing tcp recv loop for %s",, spec_ops->opt->filename);
   break;
   }
   }
   if(spec_ops->inc != 0)
   {
   spec_ops->opt->cumul++;
   unsigned long n_now = add_to_packets(spec_ops->opt->fi, spec_ops->opt->buf_num_elems);
   D("A buffer filled for %s. Next file: %ld. Packets now %ld",, spec_ops->opt->filename, spec_ops->opt->cumul, n_now);
   free_the_buf(se->be);
   se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch,spec_ops->opt ,&(spec_ops->opt->cumul), NULL,1);
   CHECK_AND_EXIT(se->be);
   se->be->simple_get_writebuf(se->be, spec_ops->inc);
   }
   }

   LOG("%s Saved %lu files and %lu packets\n",spec_ops->opt->filename, spec_ops->opt->cumul, spec_ops->opt->total_packets);

   return 0;
   }
   */
