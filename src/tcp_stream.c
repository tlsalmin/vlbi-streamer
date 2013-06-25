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
  err = create_socket(&(spec_ops->fd), port, &(spec_ops->servinfo), spec_ops->opt->hostname, SOCK_STREAM, &(spec_ops->p), spec_ops->opt->optbits, spec_ops->opt->address_to_bind_to);
  CHECK_ERR("Create socket");
  if(!(opt->optbits & READMODE) && opt->hostname != NULL){
    char port[12];
    memset(port, 0,sizeof(char)*12);
    sprintf(port,"%d", spec_ops->opt->port);
    err = create_socket(&(spec_ops->fd_send), port, &(spec_ops->servinfo_simusend), spec_ops->opt->hostname, SOCK_STREAM, &(spec_ops->p_send), spec_ops->opt->optbits, NULL);
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
    err = listen(spec_ops->fd, spec_ops->opt->stream_multiply);
    LOG("Stream multiply is %d\n", spec_ops->opt->stream_multiply);
    CHECK_ERR("listen to socket");
  }

  return 0;
}
int change_buffer(struct streamer_entity *se, void**buf, uint64_t ** buf_incrementer)
{
  struct socketopts * spec_ops = (struct socketopts*)se->opt;
  spec_ops->opt->cumul++;
  spec_ops->opt->total_packets += spec_ops->opt->buf_num_elems;
  unsigned long n_now = add_to_packets(spec_ops->opt->fi, spec_ops->opt->buf_num_elems);
  D("A buffer filled for %s. Next file: %ld. Packets now %ld",, spec_ops->opt->filename, spec_ops->opt->cumul, n_now);
  free_the_buf(se->be);
  se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch,spec_ops->opt ,&(spec_ops->opt->cumul), NULL,1);
  CHECK_AND_EXIT(se->be);
  *buf = se->be->simple_get_writebuf(se->be, buf_incrementer);
  return 0;
}

int handle_received_bytes(struct streamer_entity *se, long err, uint64_t bufsize, uint64_t ** buf_incrementer, void** buf)
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
  //spec_ops->opt->total_packets += err/spec_ops->opt->packet_size;
  **buf_incrementer += err;
  if(**buf_incrementer == bufsize)
  {
    err = change_buffer(se, buf, buf_incrementer);
    CHECK_ERR("Change buffer");
  }
  return 0;
}

int loop_with_splice(struct streamer_entity *se)
{
  long err;
  struct socketopts * spec_ops = (struct socketopts*)se->opt;
  unsigned long *buf_incrementer;

  se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch, spec_ops->opt,&(spec_ops->opt->cumul), NULL,1);
  CHECK_AND_EXIT(se->be);

  void *buf = se->be->simple_get_writebuf(se->be, &buf_incrementer);
  uint64_t bufsize = CALC_BUFSIZE_FROM_OPT(spec_ops->opt);
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

  se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch, spec_ops->opt,&(spec_ops->opt->cumul), NULL,1);
  CHECK_AND_EXIT(se->be);

  void *buf = se->be->simple_get_writebuf(se->be, &buf_incrementer);
  long bufsize = CALC_BUFSIZE_FROM_OPT(spec_ops->opt);

  while(get_status_from_opt(spec_ops->opt) & STATUS_RUNNING)
  {
    /* In local area receive using the packet_size as recv-argument seems to yield better results       */
    /* This is set as an option, since long range transfers might benefit by getting a larger planning  */
    /* window when using the whole buffer as a size                                                     */
    if(spec_ops->opt->optbits & USE_LARGEST_TRANSAC)
      err = recv(spec_ops->tcp_fd, buf+*buf_incrementer, (bufsize - *buf_incrementer), 0);
    else
      err = recv(spec_ops->tcp_fd, buf+*buf_incrementer, MIN(spec_ops->opt->packet_size,(bufsize-*buf_incrementer)), 0);
    err = handle_received_bytes(se, err, bufsize, &buf_incrementer, &buf);
    if(err != 0){
      D("Finishing tcp recv loop for %s",, spec_ops->opt->filename);
      break;
    }
  }
  if(*buf_incrementer == 0 || *buf_incrementer < spec_ops->opt->packet_size){
    *buf_incrementer =0 ;
    se->be->cancel_writebuf(se->be);
    se->be = NULL;
    D("Writebuf cancelled, since it was empty");
  }
  else{
    unsigned long n_now = add_to_packets(spec_ops->opt->fi, (*buf_incrementer)/spec_ops->opt->packet_size);
    D("N packets is now %lu and received nu, %lu",, n_now, spec_ops->opt->total_packets);
    spec_ops->opt->cumul++;
    spec_ops->opt->total_packets += (*buf_incrementer)/spec_ops->opt->packet_size;
    se->be->set_ready_and_signal(se->be,0);
  }

  LOG("%s Saved %lu files and %lu packets\n",spec_ops->opt->filename, spec_ops->opt->cumul, spec_ops->opt->total_packets);
  if(!(get_status_from_opt(spec_ops->opt) & STATUS_ERROR))
    set_status_for_opt(spec_ops->opt, STATUS_STOPPED);

  return 0;
}

void * threaded_multistream_recv(void * data)
{
  uint64_t offset;
  uint64_t packets_ready;
  uint64_t max_packets;
  int err;
  void * correct_bufspot;
  struct multistream_recv_data* td = (struct multistream_recv_data*)data;
  struct socketopts *spec_ops = td->spec_ops;

  while(td->status & THREADSTATUS_KEEP_RUNNING)
  {
    pthread_mutex_lock(td->recv_mutex);
    while(td->status & THREADSTATUS_ALL_FULL)
    {
      pthread_cond_wait(td->recv_cond, td->recv_mutex);
    }
    if(!(td->status & THREADSTATUS_KEEP_RUNNING)){
      D("Exit called for all threads");
      break;
    }
    pthread_mutex_unlock(td->recv_mutex);
    packets_ready =0;
    td->bytes_received=0;
    offset = 0;

    max_packets = (spec_ops->opt->buf_num_elems-((spec_ops->opt->stream_multiply-*(td->start_remainder))+*(td->end_remainder)))/spec_ops->opt->stream_multiply;
    if(td->seq >= *(td->start_remainder))
      max_packets++;
    if(td->seq < (*td->end_remainder))
      max_packets++;

    correct_bufspot = td->buf+(td->seq*spec_ops->opt->packet_size);

    while(packets_ready < max_packets)
    {
      err = recv(td->fd, correct_bufspot+offset, spec_ops->opt->packet_size-offset, 0);
      if(err < 0)
      {
	E("Error in recv of a thread");
	td->status = THREADSTATUS_ERROR;
	break;
      }
      else if(err == 0){
	D("Clean exit on thread %d",, td->seq);
	td->status = THREADSTATUS_CLEAN_EXIT;
	break;
      }
      if(err+offset == spec_ops->opt->packet_size)
      {
	packets_ready++;
	correct_bufspot += spec_ops->opt->packet_size*spec_ops->opt->stream_multiply;
	offset=0;
	td->bytes_received += err;
      }
      else{
	offset+=err;
	td->bytes_received += err;
      }
    }
    pthread_mutex_lock(td->mainthread_mutex);
    if(packets_ready == max_packets){
      td->status |= THREADSTATUS_ALL_FULL;
    }
    *(td->threads_ready_or_err) += 1;
    pthread_cond_signal(td->mainthread_cond);
    pthread_mutex_unlock(td->mainthread_mutex);
  }
  pthread_exit(NULL);
}
int loop_with_threaded_multistream_recv(struct streamer_entity *se)
{
  struct socketopts * spec_ops = (struct socketopts*)se->opt;
  long err;
  int i;
  uint64_t *buf_incrementer;
  int threads_ready_or_err =0;
  int active_threads = spec_ops->opt->stream_multiply;
  int errorflag=0;
  pthread_mutex_t recv_mutex;
  pthread_cond_t recv_cond;
  pthread_mutex_t mainthread_mutex;
  pthread_cond_t mainthread_cond;
  struct multistream_recv_data *threads = malloc(sizeof(struct multistream_recv_data)*spec_ops->opt->stream_multiply);
  memset(threads, 0 , sizeof(struct multistream_recv_data*)*spec_ops->opt->stream_multiply);

  err = pthread_mutex_init(&recv_mutex, NULL);
  CHECK_ERR("recv mutex init");
  err = pthread_mutex_init(&mainthread_mutex, NULL);
  CHECK_ERR("maintread mutex init");
  err = pthread_cond_init(&recv_cond, NULL);
  CHECK_ERR("recv cond init");
  err = pthread_cond_init(&mainthread_cond, NULL);
  CHECK_ERR("mainthread cond init");

  se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch, spec_ops->opt,&(spec_ops->opt->cumul), NULL,1);
  if(se->be == NULL)
  {
    E("Error in getting a buffer. Exiting");
    free(threads);
    return -1;
  }

  void *buf = se->be->simple_get_writebuf(se->be, &buf_incrementer);
  int end_remainder = spec_ops->opt->buf_num_elems % spec_ops->opt->stream_multiply;
  int start_remainder = spec_ops->opt->stream_multiply;

  /* Formatting loop	*/
  for(i=0;i<spec_ops->opt->stream_multiply;i++)
  {
    threads[i].cumul = &spec_ops->opt->cumul;
    threads[i].start_remainder = &start_remainder;
    threads[i].end_remainder = &end_remainder;
    threads[i].fd = spec_ops->fds_for_multiply[i];
    threads[i].buf = buf;
    threads[i].seq = i;
    threads[i].spec_ops = spec_ops;
    threads[i].recv_mutex = &recv_mutex;
    threads[i].mainthread_mutex = &mainthread_mutex;
    threads[i].recv_cond = &recv_cond;
    threads[i].mainthread_cond= &mainthread_cond;
    threads[i].status |= THREADSTATUS_KEEP_RUNNING;
    threads[i].threads_ready_or_err = &threads_ready_or_err;
    err = pthread_create(&threads[i].pt, NULL, threaded_multistream_recv, (void*)&threads[i]);
    if(err != 0){
      E("Error in creating receiving thread");
      free(threads);
      return -1;
    }
  }

  /* Mainloop		*/
  while(get_status_from_opt(spec_ops->opt) & STATUS_RUNNING && active_threads > 0)
  {
    D("Starting main loop");
    pthread_mutex_lock(&mainthread_mutex);
    while(threads_ready_or_err < active_threads)
      pthread_cond_wait(&mainthread_cond, &mainthread_mutex);
    pthread_mutex_unlock(&mainthread_mutex);

    D("All threads ready");
    for(i=0;i<spec_ops->opt->stream_multiply;i++)
    {
      spec_ops->total_transacted_bytes += threads[i].bytes_received;
      *buf_incrementer += threads[i].bytes_received;
      if(threads[i].status & THREADSTATUS_CLEAN_EXIT){
	active_threads--;
	pthread_join(threads[i].pt, NULL);
	D("One thread had clean exit");
      }
      if(threads[i].status & THREADSTATUS_ERROR){
	D("One thread ended in err. Stopping everything");
	active_threads = 0;
	errorflag=1;
	break;
      }
    }
    if(errorflag ==1 || active_threads ==0 ){
      D("Breaking out of main loop");
      break;
    }

    if(end_remainder == 0)
      start_remainder = spec_ops->opt->stream_multiply;
    else
      start_remainder = end_remainder;
    end_remainder = (spec_ops->opt->buf_num_elems-(spec_ops->opt->stream_multiply-end_remainder)) % spec_ops->opt->stream_multiply;

    D("Chancing buffer");
    err = change_buffer(se, &buf, &buf_incrementer);

    if(err != 0)
      break;

    pthread_mutex_lock(&recv_mutex);
    for(i=0;i<spec_ops->opt->stream_multiply;i++)
    {
      /* Clears the full flag	*/
      if(threads[i].status & THREADSTATUS_KEEP_RUNNING)
	threads[i].status = THREADSTATUS_KEEP_RUNNING;
    }
    D("Broadcasting for threads to continue");
    pthread_cond_broadcast(&recv_cond);
    threads_ready_or_err=0;
    pthread_mutex_unlock(&recv_mutex);
    D("Buffer complete in multithreaded");
  }
  /* Double rundown loop	*/
  pthread_mutex_lock(&recv_mutex);
  for(i=0;i<spec_ops->opt->stream_multiply;i++)
  {
    *buf_incrementer += threads[i].bytes_received;
    threads[i].status = 0;
    if(threads[i].fd != 0)
    {
      shutdown(threads[i].fd, SHUT_RD);
      close(threads[i].fd);
    }
  }
  pthread_cond_broadcast(&recv_cond);
  pthread_mutex_unlock(&recv_mutex);
  for(i=0;i<spec_ops->opt->stream_multiply;i++)
  {
    pthread_join(threads[i].pt, NULL);
  }
  if(*buf_incrementer == 0 || *buf_incrementer < spec_ops->opt->packet_size){
    *buf_incrementer =0 ;
    se->be->cancel_writebuf(se->be);
    se->be = NULL;
    D("Writebuf cancelled, since it was empty");
  }
  else{
    unsigned long n_now = add_to_packets(spec_ops->opt->fi, (*buf_incrementer)/spec_ops->opt->packet_size);
    D("N packets is now %lu and received nu, %lu",, n_now, spec_ops->opt->total_packets);
    spec_ops->opt->cumul++;
    spec_ops->opt->total_packets += (*buf_incrementer)/spec_ops->opt->packet_size;
    se->be->set_ready_and_signal(se->be,0);
  }

  pthread_mutex_destroy(&recv_mutex);
  pthread_mutex_destroy(&mainthread_mutex);
  pthread_cond_destroy(&recv_cond);
  pthread_cond_destroy(&mainthread_cond);

  free(threads);

  LOG("%s Saved %lu files and %lu packets\n",spec_ops->opt->filename, spec_ops->opt->cumul, spec_ops->opt->total_packets);
  if(!(get_status_from_opt(spec_ops->opt) & STATUS_ERROR))
    set_status_for_opt(spec_ops->opt, STATUS_STOPPED);

  return 0;
}
int loop_with_multistream_recv(struct streamer_entity *se)
{
  struct socketopts * spec_ops = (struct socketopts*)se->opt;
  long err;
  int stream_iterator=0;
  int fd_now;
  uint64_t *buf_incrementer;
  uint32_t offsets_for_multiply[spec_ops->opt->stream_multiply];
  uint32_t pready_for_multiply[spec_ops->opt->stream_multiply];
  memset(&offsets_for_multiply, 0, sizeof(uint32_t)*spec_ops->opt->stream_multiply);
  memset(&pready_for_multiply, 0, sizeof(uint32_t)*spec_ops->opt->stream_multiply);

  se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch, spec_ops->opt,&(spec_ops->opt->cumul), NULL,1);
  CHECK_AND_EXIT(se->be);

  void *buf = se->be->simple_get_writebuf(se->be, &buf_incrementer);
  uint64_t bufsize = CALC_BUFSIZE_FROM_OPT(spec_ops->opt);
  int remainder = spec_ops->opt->buf_num_elems % spec_ops->opt->stream_multiply;

  while(get_status_from_opt(spec_ops->opt) & STATUS_RUNNING)
  {
    fd_now = spec_ops->fds_for_multiply[stream_iterator];
    if(fd_now != 0)
    {
      err = recv(fd_now, buf+stream_iterator*spec_ops->opt->packet_size+offsets_for_multiply[stream_iterator], (spec_ops->opt->packet_size-offsets_for_multiply[stream_iterator]), 0);
      if(err < 0){
	//err = handle_received_bytes(se, err, bufsize, &buf_incrementer, &buf);
	E("Error in tcp loop of transfer %s",, spec_ops->opt->filename);
	break;
      }
      else if(err==0)
      {
	D("Closed %d socket for filename %s",, stream_iterator, spec_ops->opt->filename);
	close(fd_now);
	spec_ops->fds_for_multiply[stream_iterator] = 0;
      }
      else
      {
	spec_ops->total_transacted_bytes += err;
	offsets_for_multiply[stream_iterator] += err;
	if(offsets_for_multiply[stream_iterator] == spec_ops->opt->packet_size){
	  pready_for_multiply[stream_iterator]++;
	  offsets_for_multiply[stream_iterator] = 0;
	}
	*buf_incrementer+=err;
      }
      if(*buf_incrementer == bufsize)
      {
	remainder = (remainder + spec_ops->opt->buf_num_elems) % spec_ops->opt->stream_multiply;
      }
    }
    stream_iterator = (stream_iterator+1)%spec_ops->opt->stream_multiply;
  }
  if(*buf_incrementer == 0 || *buf_incrementer < spec_ops->opt->packet_size){
    *buf_incrementer =0 ;
    se->be->cancel_writebuf(se->be);
    se->be = NULL;
    D("Writebuf cancelled, since it was empty");
  }
  else{
    unsigned long n_now = add_to_packets(spec_ops->opt->fi, (*buf_incrementer)/spec_ops->opt->packet_size);
    D("N packets is now %lu and received nu, %lu",, n_now, spec_ops->opt->total_packets);
    spec_ops->opt->cumul++;
    spec_ops->opt->total_packets += (*buf_incrementer)/spec_ops->opt->packet_size;
    se->be->set_ready_and_signal(se->be,0);
  }

  LOG("%s Saved %lu files and %lu packets\n",spec_ops->opt->filename, spec_ops->opt->cumul, spec_ops->opt->total_packets);
  if(!(get_status_from_opt(spec_ops->opt) & STATUS_ERROR))
    set_status_for_opt(spec_ops->opt, STATUS_STOPPED);

  return 0;
}
int tcp_sendcmd(struct streamer_entity* se, struct sender_tracking *st)
{
  int err;
  struct socketopts *spec_ops = se->opt;

  err = send(spec_ops->fd, se->be->buffer+(*spec_ops->inc), MIN(spec_ops->opt->packet_size, st->packetcounter), MSG_NOSIGNAL);
  // Increment to the next sendable packet
  if(err < 0){
    perror("Send stream data");
    se->close_socket(se);
    return -1;
  }
  else if(err == 0){
    LOG("Remote end shut down for sending %s\n", spec_ops->opt->filename);
    return -1;
  }
  /* Proper counting for TCP. Might be half a packet etc so we need to save reminder etc.	*/
  else{
    //st->packets_sent+=
    spec_ops->total_transacted_bytes +=err;
    st->packetcounter -= err;
    *(spec_ops->inc) += err;
    //st->packetcounter++;
  }
  return 0;
}
int tcp_sendfilecmd(struct streamer_entity* se, struct sender_tracking *st)
{
  int err;
  struct socketopts *spec_ops = se->opt;
  err = sendfile(se->be->get_shmid(se->be), spec_ops->fd, (off_t*)spec_ops->inc, st->packetcounter);
  // Increment to the next sendable packet
  if(err < 0){
    perror("Send stream data");
    se->close_socket(se);
    return -1;
  }
  /* Proper counting for TCP. Might be half a packet etc so we need to save reminder etc.	*/
  else{
    /* sendfile increments inc	*/
    spec_ops->total_transacted_bytes +=err;
    st->packetcounter -= err;
  }
  return 0;
}
void* tcp_preloop(void *ser)
{
  int err,i;
  struct streamer_entity * se = (struct streamer_entity *)ser;
  struct socketopts *spec_ops = (struct socketopts*)se->opt;
  reset_udpopts_stats(spec_ops);

  if(!(spec_ops->opt->optbits & READMODE))
  {
    spec_ops->sin_l = sizeof(struct sockaddr);
    memset(&spec_ops->sin, 0, sizeof(struct sockaddr));
    if(spec_ops->opt->optbits & CAPTURE_W_TCPSTREAM){
      if((spec_ops->tcp_fd = accept(spec_ops->fd, (struct sockaddr*)&(spec_ops->sin), &(spec_ops->sin_l))) < 0)
      {
	E("Error in accepting socket %d",, spec_ops->fd);
	se->close_socket(se);
	set_status_for_opt(spec_ops->opt, STATUS_ERROR);
	pthread_exit(NULL);
      }
    }
    else
    {
      spec_ops->fds_for_multiply = malloc(sizeof(int)*spec_ops->opt->stream_multiply);
      for(i=0;i<spec_ops->opt->stream_multiply;i++)
      {
	if((spec_ops->fds_for_multiply[i] = accept(spec_ops->fd, (struct sockaddr*)&(spec_ops->sin), &(spec_ops->sin_l))) < 0)
	{
	  E("Error in accepting socket %d",, spec_ops->fd);
	  se->close_socket(se);
	  set_status_for_opt(spec_ops->opt, STATUS_ERROR);
	  pthread_exit(NULL);
	}
      }
    }
    char s[INET6_ADDRSTRLEN];
    /* Stolen from the great Beejs network guide	*/
    /* http://beej.us/guide/bgnet/	*/
    inet_ntop(spec_ops->sin.ss_family,
	get_in_addr((struct sockaddr *)&spec_ops->sin),
	s, sizeof s);
    LOG("server: got connection from %s\n", s);
  }
  GETTIME(spec_ops->opt->starting_time);

  LOG("TCP_STREAMER: Starting stream capture\n");
  switch (spec_ops->opt->optbits & LOCKER_CAPTURE)
  {
    case(CAPTURE_W_TCPSTREAM):
      if(spec_ops->opt->optbits & READMODE)
      err = generic_sendloop(se, 0, tcp_sendcmd, bboundary_bytenum);
      else
	err = loop_with_recv(se);
      break;
    case(CAPTURE_W_MULTISTREAM):
      if (spec_ops->opt->optbits & READMODE)
      err = generic_sendloop(se, 0, tcp_sendcmd, bboundary_bytenum);
      else
	err = loop_with_threaded_multistream_recv(se);
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
  se->close_socket(se);
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
