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

#include <sys/sendfile.h>
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
  struct socketopts *spec_ops = (struct socketopts*)sp;
  //if(spec_ops->opt->optbits & USE_RX_RING)
  stat->total_packets += spec_ops->opt->total_packets;
  stat->total_bytes += spec_ops->total_transacted_bytes;
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
  struct socketopts *spec_ops =(struct socketopts *) malloc(sizeof(struct socketopts));
  memset(spec_ops, 0, sizeof(struct socketopts));
  CHECK_ERR_NONNULL(spec_ops, "spec ops malloc");

  se->opt = (void*)spec_ops;

  spec_ops->opt = opt;
  opt->optbits |= CONNECT_BEFORE_SENDING;

  spec_ops->fd = -1;
  char port[12];
  memset(port, 0,sizeof(char)*12);
  sprintf(port,"%d", spec_ops->opt->port);
  if(!(spec_ops->opt->optbits & READMODE))
    spec_ops->opt->optbits |= SO_REUSEIT;
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
    err = listen(spec_ops->fd, 1);
    CHECK_ERR("listen to socket");
    spec_ops->sin_l = sizeof(struct sockaddr);
    memset(&spec_ops->sin, 0, sizeof(struct sockaddr));
    if((spec_ops->tcp_fd = accept(spec_ops->fd, (struct sockaddr*)&(spec_ops->sin), &(spec_ops->sin_l))) < 0)
    {
      E("Error in accepting socket %d",, spec_ops->fd);
      return -1;
    }
    char s[INET6_ADDRSTRLEN];
    /* Stolen from the great Beejs network guide	*/
    /* http://beej.us/guide/bgnet/	*/
    inet_ntop(spec_ops->sin.ss_family,
	get_in_addr((struct sockaddr *)&spec_ops->sin),
	s, sizeof s);
    LOG("server: got connection from %s\n", s);
  }

  return 0;
}

int handle_received_bytes(struct streamer_entity *se, long err, long bufsize, unsigned long ** buf_incrementer, void** buf)
{
  struct socketopts * spec_ops = (struct socketopts*)se->opt;
  if(err < 0){
    E("Error in splice receive");
    return -1;
  }
  else if(err == 0)
  {
    D("Socket shutdown");
    return 1;
  }
  /*
  else if(err != (bufsize-**buf_incrementer))
    D("Supposed to send %ld but sent only %ld",, (bufsize-**buf_incrementer), err);
  else
    D("Sent %ld as told",, err);
    */
  spec_ops->total_transacted_bytes += err;
  spec_ops->opt->total_packets += err/spec_ops->opt->packet_size;
  **buf_incrementer += err;
  if(**buf_incrementer == bufsize)
  {
    spec_ops->opt->cumul++;
    unsigned long n_now = add_to_packets(spec_ops->opt->fi, spec_ops->opt->buf_num_elems);
    D("A buffer filled for %s. Next file: %ld. Packets now %ld",, spec_ops->opt->filename, spec_ops->opt->cumul, n_now);
    free_the_buf(se->be);
    se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch,spec_ops->opt ,&(spec_ops->opt->cumul), NULL,1);
    CHECK_AND_EXIT(se->be);
    *buf = se->be->simple_get_writebuf(se->be, buf_incrementer);
  }

  return 0;
}

int loop_with_splice(struct streamer_entity *se)
{
  long err;
  struct socketopts * spec_ops = (struct socketopts*)se->opt;
  unsigned long *buf_incrementer;
  void *buf = se->be->simple_get_writebuf(se->be, &buf_incrementer);
  long bufsize = CALC_BUFSIZE_FROM_OPT(spec_ops->opt);
  int fd_out = se->be->get_shmid(se->be);

  while(get_status_from_opt(spec_ops->opt) & STATUS_RUNNING)
  {
    err = sendfile(fd_out, spec_ops->tcp_fd, NULL, (bufsize - *buf_incrementer));
    err = handle_received_bytes(se, err, bufsize, &buf_incrementer, &buf);
    if(err != 0){
      D("Finishing tcp splice loop for %s",, spec_ops->opt->filename);
      break;
    }
  }
  if(*buf_incrementer == 0){
    se->be->cancel_writebuf(se->be);
    se->be = NULL;
  }
  else{
      unsigned long n_now = add_to_packets(spec_ops->opt->fi, (*buf_incrementer)/spec_ops->opt->packet_size);
      D("N packets is now %lu and received nu, %lu",, n_now, spec_ops->opt->total_packets);
    spec_ops->opt->cumul++;
    se->be->set_ready_and_signal(se->be,0);
  }

  LOG("%s Saved %lu files and %lu packets\n",spec_ops->opt->filename, spec_ops->opt->cumul, spec_ops->opt->total_packets);


  return 0;
}
int loop_with_recv(struct streamer_entity *se)
{
  long err;
  struct socketopts * spec_ops = (struct socketopts*)se->opt;
  unsigned long *buf_incrementer;
  void *buf = se->be->simple_get_writebuf(se->be, &buf_incrementer);
  long bufsize = CALC_BUFSIZE_FROM_OPT(spec_ops->opt);
  //int fd_out = se->be->get_shmid(se->be);
  //long request;

  while(get_status_from_opt(spec_ops->opt) & STATUS_RUNNING)
  {
    //request = MIN(
    err = recv(spec_ops->tcp_fd, buf, (bufsize - *buf_incrementer), 0);
    err = handle_received_bytes(se, err, bufsize, &buf_incrementer, &buf);
    if(err != 0){
      D("Finishing tcp recv loop for %s",, spec_ops->opt->filename);
      break;
    }
  }
  if(*buf_incrementer == 0){
    se->be->cancel_writebuf(se->be);
    se->be = NULL;
  }
  else{
      unsigned long n_now = add_to_packets(spec_ops->opt->fi, (*buf_incrementer)/spec_ops->opt->packet_size);
      D("N packets is now %lu and received nu, %lu",, n_now, spec_ops->opt->total_packets);
    spec_ops->opt->cumul++;
    se->be->set_ready_and_signal(se->be,0);
  }

  LOG("%s Saved %lu files and %lu packets\n",spec_ops->opt->filename, spec_ops->opt->cumul, spec_ops->opt->total_packets);


  return 0;
}
int tcp_sendcmd(struct streamer_entity* se, struct sender_tracking *st)
{
  int err;
  struct socketopts *spec_ops = se->opt;
  //size_t tosend = MIN(st->total_bytes_to_send-spec_ops->total_transacted_bytes, spec_ops->opt->buf_num_elems*spec_ops->opt->packet_size-st->inc);
  err = send(spec_ops->fd, se->be->buffer+(*spec_ops->inc), st->packetcounter, 0);
  // Increment to the next sendable packet
  if(err < 0){
    perror("Send stream data");
    se->close_socket(se);
    return -1;
  }
  /* Proper counting for TCP. Might be half a packet etc so we need to save reminder etc.	*/
  else{
    //st->packets_sent+=
    spec_ops->total_transacted_bytes +=err;
    st->packetcounter -= err;
    spec_ops->inc += err;
    //st->packetcounter++;
  }
  return 0;
}
int tcp_sendfilecmd(struct streamer_entity* se, struct sender_tracking *st)
{
  int err;
  struct socketopts *spec_ops = se->opt;
  //size_t tosend = MIN(st->total_bytes_to_send-spec_ops->total_transacted_bytes, spec_ops->opt->buf_num_elems*spec_ops->opt->packet_size-st->inc);
  err = sendfile(se->be->get_shmid(se->be), spec_ops->fd, 0, st->packetcounter);
  // Increment to the next sendable packet
  if(err < 0){
    perror("Send stream data");
    se->close_socket(se);
    return -1;
  }
  /* Proper counting for TCP. Might be half a packet etc so we need to save reminder etc.	*/
  else{
    //st->packets_sent+=
    spec_ops->total_transacted_bytes +=err;
    st->packetcounter -= err;
    spec_ops->inc += err;
    //st->packetcounter++;
  }
  return 0;
}
void* tcp_preloop(void *ser)
{
  int err;
  struct streamer_entity * se = (struct streamer_entity *)ser;
  struct socketopts *spec_ops = (struct socketopts*)se->opt;
  reset_udpopts_stats(spec_ops);

  se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch, spec_ops->opt,&(spec_ops->opt->cumul), NULL,1);
  CHECK_AND_EXIT(se->be);

  LOG("TCP_STREAMER: Starting stream capture\n");
  switch (spec_ops->opt->optbits & LOCKER_CAPTURE)
  {
    case(CAPTURE_W_TCPSTREAM):
      if (spec_ops->opt->optbits & READMODE)
	err = generic_sendloop(se, 0, tcp_sendcmd, bboundary_bytenum);
      else
	err = loop_with_recv(se);
      break;
    case(CAPTURE_W_TCPSPLICE):
      if (spec_ops->opt->optbits & READMODE)
	err =  generic_sendloop(se, 0, tcp_sendfilecmd, bboundary_bytenum);
      else
	err = loop_with_splice(se);
      break;
    default:
      E("Undefined recceive loop");
      err = -1;
    break;
  }
  if(err != 0)
    E("Loop stopped in error");
  D("Saved %lu files and %lu bytes",, spec_ops->opt->cumul, spec_ops->total_transacted_bytes);
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
