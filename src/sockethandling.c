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
//#include "common_filehandling.h"
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
int create_socket(int *fd, char * port, struct addrinfo ** servinfo, char * hostname, int socktype, struct addrinfo ** used, uint64_t optbits)
{
  int err;
  struct addrinfo hints, *p;
  memset(&hints, 0, sizeof(struct addrinfo));
  /* Great ipv6 guide http://beej.us/guide/bgnet/					*/
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = socktype;
  hints.ai_flags = AI_PASSIVE;
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
      E("Cant bind to %s. Trying next",, p->ai_canonname);
      continue;
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
  struct udpopts *spec_ops = se->opt;
  if(spec_ops->fd == 0)
  {
    D("Wont shut down already shutdown fd");
  }
  else
  {
    int ret;
    if(spec_ops->opt->optbits & READMODE){
      LOG("Closing socket on send of %s to %s\n", spec_ops->opt->filename, spec_ops->opt->hostname);
      ret = shutdown(spec_ops->fd, SHUT_WR);
      ret = close(spec_ops->fd);
    }
    else{
      LOG("Closing socket on receive %s\n", spec_ops->opt->filename);
      ret = shutdown(spec_ops->fd, SHUT_RD);
      if(ret <0)
	E("shutdown return something not ok");
      ret = close(spec_ops->fd);
    }
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
  struct udpopts *spec_ops = (struct udpopts *)se->opt;
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
     if(!(spec_ops->opt->optbits & USE_RX_RING))
     free(spec_ops->sin);
     */
  free(spec_ops);
  D("Returning");
  return 0;
}
void stop_streamer(struct streamer_entity *se){
  D("Stopping loop");
  struct udpopts* spec_ops = (struct udpopts*)se->opt;
  set_status_for_opt(spec_ops->opt, STATUS_STOPPED);
  close_socket(se);
}
void reset_udpopts_stats(struct udpopts *spec_ops)
{
  spec_ops->wrongsizeerrors = 0;
  spec_ops->total_captured_bytes = 0;
  spec_ops->opt->total_packets = 0;
  spec_ops->out_of_order = 0;
  spec_ops->incomplete = 0;
  spec_ops->missing = 0;
}
