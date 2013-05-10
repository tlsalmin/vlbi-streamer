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
/* Not sure if needed	*/
#include "tcp_stream.h"

extern FILE* logfile;

/* TODO: Not using packets per se so need to get his more consistent 	*/
void get_tcp_stats(void *sp, void *stats){
  struct stats *stat = (struct stats * ) stats;
  struct udpopts *spec_ops = (struct udpopts*)sp;
  //if(spec_ops->opt->optbits & USE_RX_RING)
  stat->total_packets += spec_ops->opt->total_packets;
  stat->total_bytes += spec_ops->total_captured_bytes;
  stat->incomplete += spec_ops->incomplete;
  stat->dropped += spec_ops->missing;
  if(spec_ops->opt->last_packet > 0){
    stat->progress = (spec_ops->opt->total_packets*100)/(spec_ops->opt->last_packet);
  }
  else
    stat->progress = -1;
  //stat->files_exchanged = udps_get_fileprogress(spec_ops);
}

int setup_tcp_socket(struct opt_s *opt, struct streamer_entity *se)
{
  int err;
  struct udpopts *spec_ops =(struct udpopts *) malloc(sizeof(struct udpopts));
  memset(spec_ops, 0, sizeof(struct udpopts));
  CHECK_ERR_NONNULL(spec_ops, "spec ops malloc");

  se->opt = (void*)spec_ops;

  spec_ops->opt = opt;
  opt->optbits |= CONNECT_BEFORE_SENDING;

  spec_ops->fd = -1;
  char port[12];
  memset(port, 0,sizeof(char)*12);
  sprintf(port,"%d", spec_ops->opt->port);
  err = create_socket(&(spec_ops->fd), port, &(spec_ops->servinfo), spec_ops->opt->hostname, SOCK_STREAM, &(spec_ops->p), spec_ops->opt->optbits);
  CHECK_ERR("Create socket");
  if(!(opt->optbits & READMODE) && opt->hostname != NULL){
    char port[12];
    memset(port, 0,sizeof(char)*12);
    sprintf(port,"%d", spec_ops->opt->port);
    err = create_socket(&(spec_ops->fd_send), port, &(spec_ops->servinfo_simusend), spec_ops->opt->hostname, SOCK_STREAM, &(spec_ops->p_send), spec_ops->opt->optbits);
    if(err != 0)
      E("Error in creating simusend socket. Not quitting");
  }

  if (spec_ops->fd < 0) {
    perror("socket");
    INIT_ERROR
  }
  err = socket_common_init_stuff(spec_ops->opt, MODE_FROM_OPTS, &(spec_ops->fd));
  CHECK_ERR("Common init");

  if(!(spec_ops->opt->optbits & READMODE)){
    spec_ops->sin_l = sizeof(struct sockaddr);
    if((spec_ops->tcp_fd = accept(spec_ops->fd, &(spec_ops->sin), &(spec_ops->sin_l))) < 0)
    {
      E("Error in accepting socket");
      return -1;
    }
  }

  return 0;
}
int loop_with_splice(struct streamer_entity *se)
{
  (void)se;
  //void *buf = se->be->simple_get_writebuf(se->be, &resq->inc);
  return 0;
}
int loop_with_recv(struct streamer_entity *se)
{
  (void)se;
  //void *buf = se->be->simple_get_writebuf(se->be, &resq->inc);
  return 0;
}
void* tcp_preloop(void *ser)
{
  int err;
  struct streamer_entity * se = (struct streamer_entity *)ser;
  struct udpopts *spec_ops = (struct udpopts*)se->opt;
  reset_udpopts_stats(spec_ops);

  se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch, spec_ops->opt,&(spec_ops->opt->cumul), NULL,1);
  CHECK_AND_EXIT(se->be);

  LOG("TCP_STREAMER: Starting stream capture\n");
  if(spec_ops->opt->optbits & CAPTURE_W_TCPSTREAM)
    err = loop_with_recv(se);
  else if (spec_ops->opt->optbits & CAPTURE_W_TCPSPLICE)
    err = loop_with_splice(se);
  if(err != 0)
    E("Loop stopped in error");
  D("Saved %lu files and %lu bytes",, spec_ops->opt->cumul, spec_ops->total_captured_bytes);
  pthread_exit(NULL);
}
int tcp_init(struct opt_s* opt, struct streamer_entity * se)
{
  se->init = setup_tcp_socket;
  se->close = close_streamer_opts;
  se->get_stats = get_tcp_stats;
  se->close_socket = close_socket;
  se->stop = stop_streamer;
  se->start = tcp_preloop;
  return se->init(opt, se);
}
