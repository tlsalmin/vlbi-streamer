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

  if(spec_ops->opt->optbits & CAPTURE_W_MULTISTREAM)
  {
    spec_ops->td = malloc(sizeof(struct multistream_recv_data)*spec_ops->opt->stream_multiply);
    CHECK_ERR_NONNULL_AUTO(spec_ops->td);
    memset(spec_ops->td, 0, sizeof(struct multistream_recv_data)*spec_ops->opt->stream_multiply);

    spec_ops->bufdata = malloc(sizeof(struct multistream_currentbufdata));
    CHECK_ERR_NONNULL_AUTO(spec_ops->bufdata);
    memset(spec_ops->bufdata, 0, sizeof(struct multistream_currentbufdata));

    spec_ops->bufdata->active_threads = spec_ops->opt->stream_multiply;

    err = pthread_mutex_init(&spec_ops->bufdata->recv_mutex, NULL);
    CHECK_ERR("recv mutex init");
    err = pthread_mutex_init(&spec_ops->bufdata->mainthread_mutex, NULL);
    CHECK_ERR("maintread mutex init");
    err = pthread_cond_init(&spec_ops->bufdata->recv_cond, NULL);
    CHECK_ERR("recv cond init");
    err = pthread_cond_init(&spec_ops->bufdata->mainthread_cond, NULL);
    CHECK_ERR("mainthread cond init");
  }

  /* Get all the sockets	*/
  if(spec_ops->opt->optbits & CAPTURE_W_MULTISTREAM && spec_ops->opt->optbits & READMODE)
  {
    int i;

    for(i=0;i<spec_ops->opt->stream_multiply;i++)
    {
      err = create_socket(&(spec_ops->td[i].fd), port, &(spec_ops->servinfo), spec_ops->opt->hostname, SOCK_STREAM, &(spec_ops->p), spec_ops->opt->optbits, spec_ops->opt->address_to_bind_to);
      /* Stuff will be freed at close_streamer_opts	*/
      CHECK_ERR("Create socket");

      err = socket_common_init_stuff(spec_ops->opt, MODE_FROM_OPTS, &(spec_ops->td[i].fd));
      CHECK_ERR("Common init");
    }
  }
  else
  {
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
  }

  return 0;
}
void* change_buffer(struct streamer_entity *se, uint64_t ** buf_incrementer)
{
  struct socketopts * spec_ops = (struct socketopts*)se->opt;
  spec_ops->opt->cumul++;
  spec_ops->opt->total_packets += spec_ops->opt->buf_num_elems;
  unsigned long n_now = add_to_packets(spec_ops->opt->fi, spec_ops->opt->buf_num_elems);
  D("A buffer filled for %s. Next file: %ld. Packets now %ld",, spec_ops->opt->filename, spec_ops->opt->cumul, n_now);
  free_the_buf(se->be);
  se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch,spec_ops->opt ,&(spec_ops->opt->cumul), NULL,1);
  if(se->be == NULL){
    E("Could not get a buffer. Returning null");
    return NULL;
  }
  return se->be->simple_get_writebuf(se->be, buf_incrementer);
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
    if(err < 0){
      E("Error in splice receive");
      break;
    }
    else if(err == 0)
    {
      D("Socket shutdown");
      break;
    }
    spec_ops->total_transacted_bytes += err;
    *buf_incrementer += err;
    if(*buf_incrementer == bufsize)
    {
      buf = change_buffer(se, &buf_incrementer);
      if(buf ==NULL)
      {
	E("Could not get a buffer");
	break;
      }
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
  uint64_t bufsize = CALC_BUFSIZE_FROM_OPT(spec_ops->opt);

  while(get_status_from_opt(spec_ops->opt) & STATUS_RUNNING)
  {
    /* In local area receive using the packet_size as recv-argument seems to yield better results       */
    /* This is set as an option, since long range transfers might benefit by getting a larger planning  */
    /* window when using the whole buffer as a size                                                     */
    if(spec_ops->opt->optbits & USE_LARGEST_TRANSAC)
      err = recv(spec_ops->tcp_fd, buf+*buf_incrementer, (bufsize - *buf_incrementer), 0);
    else
      err = recv(spec_ops->tcp_fd, buf+*buf_incrementer, MIN(spec_ops->opt->packet_size,(bufsize-*buf_incrementer)), 0);
    if(err < 0){
      E("Error in TCP receive");
      break;
    }
    else if(err == 0)
    {
      D("Socket shutdown");
      break;
    }
    spec_ops->total_transacted_bytes += err;
    *buf_incrementer += err;
    if(*buf_incrementer == bufsize)
    {
      buf = change_buffer(se, &buf_incrementer);
      if(buf ==NULL)
      {
	E("Could not get a buffer");
	break;
      }
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

void * threaded_multistream(void * data)
{
  uint64_t offset;
  uint64_t packets_ready;
  uint64_t max_packets;
  uint64_t elems_in_buf;
  int err;
  void * correct_bufspot;
  struct multistream_recv_data* td = (struct multistream_recv_data*)data;
  struct socketopts *spec_ops = td->spec_ops;
  /* For easier pointing	*/
  struct sender_tracking *st = spec_ops->bufdata->st;

  while(td->status & THREADSTATUS_KEEP_RUNNING)
  {
    pthread_mutex_lock(&spec_ops->bufdata->recv_mutex);
    while(td->status & THREADSTATUS_ALL_FULL)
    {
      pthread_cond_wait(&spec_ops->bufdata->recv_cond, &spec_ops->bufdata->recv_mutex);
    }
    if(!(td->status & THREADSTATUS_KEEP_RUNNING)){
      D("Exit called for all threads");
      pthread_mutex_unlock(&spec_ops->bufdata->recv_mutex);
      break;
    }
    pthread_mutex_unlock(&spec_ops->bufdata->recv_mutex);
    packets_ready =0;
    offset = 0;

    if((td->seq-spec_ops->bufdata->start_remainder) >= 0)
    {
      //D("FIRST");
      correct_bufspot = td->buf+((td->seq-spec_ops->bufdata->start_remainder)*spec_ops->opt->packet_size);
    }
    else{
      //D("SECOND");
      correct_bufspot = td->buf+((td->seq+(spec_ops->opt->stream_multiply-spec_ops->bufdata->start_remainder))*spec_ops->opt->packet_size);
    }

    D("Probed %ld sent %ld",, st->n_packets_probed, st->packetcounter);

    if(spec_ops->opt->optbits & READMODE && (st->packetcounter/spec_ops->opt->packet_size) < spec_ops->opt->buf_num_elems)
      elems_in_buf = st->packetcounter/spec_ops->opt->packet_size;
    else
      elems_in_buf = spec_ops->opt->buf_num_elems;

    max_packets = (elems_in_buf-((spec_ops->opt->stream_multiply-(spec_ops->bufdata->start_remainder))+(spec_ops->bufdata->end_remainder)))/spec_ops->opt->stream_multiply;
    if((td->seq - (spec_ops->bufdata->start_remainder)) >= 0)
      max_packets++;
    if(td->seq < (spec_ops->bufdata->end_remainder))
      max_packets++;

    D("Thread %d calculated to acquire %ld packets of %ld elems_in_buf",, td->seq, max_packets, elems_in_buf);

    //ASSERT((correct_bufspot+(max_packets)*(spec_ops->opt->packet_size)*spec_ops->opt->stream_multiply) <= (td->buf+CALC_BUFSIZE_FROM_OPT_NOOFFSET(spec_ops->opt)));

    while(packets_ready < max_packets)
    {
      /*
	 if(td->buf+CALC_BUFSIZE_FROM_OPT_NOOFFSET(spec_ops->opt) <= correct_bufspot+offset)
	 D("Diff is %ld, packets ready %ld, max_packets %ld",, td->buf+CALC_BUFSIZE_FROM_OPT_NOOFFSET(spec_ops->opt)- correct_bufspot+offset, packets_ready, max_packets);
	 */
      if(spec_ops->opt->optbits & READMODE)
	err = send(td->fd, correct_bufspot+offset, (spec_ops->opt->packet_size-offset), 0);
      else
	err = recv(td->fd, correct_bufspot+offset, (spec_ops->opt->packet_size-offset), 0);
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
      if(err+offset >= spec_ops->opt->packet_size)
      {
	packets_ready++;
	correct_bufspot += spec_ops->opt->packet_size*spec_ops->opt->stream_multiply;
	offset=0;
      }
      else{
	offset+=err;
      }
    }
    td->bytes_received = spec_ops->opt->packet_size*packets_ready;
    D("seq %d was supposed to get %ld packets but got %ld",, td->seq, max_packets, packets_ready);
    ASSERT(packets_ready <= max_packets);
    pthread_mutex_lock(&spec_ops->bufdata->mainthread_mutex);
    if(packets_ready == max_packets){
      td->status |= THREADSTATUS_ALL_FULL;
    }
    spec_ops->bufdata->threads_ready_or_err++;
    pthread_cond_signal(&spec_ops->bufdata->mainthread_cond);
    pthread_mutex_unlock(&spec_ops->bufdata->mainthread_mutex);
  }
  pthread_exit(NULL);
}
int loop_with_threaded_multistream_recv(struct streamer_entity *se)
{
  struct socketopts * spec_ops = (struct socketopts*)se->opt;
  long err;
  int i;
  uint64_t *buf_incrementer;
  int errorflag=0;


  se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch, spec_ops->opt,&(spec_ops->opt->cumul), NULL,1);
  if(se->be == NULL)
  {
    E("Error in getting a buffer. Exiting");
    return -1;
  }

  void *buf = se->be->simple_get_writebuf(se->be, &buf_incrementer);
  D("Dat buf_incrementer %ld",, *buf_incrementer);
  spec_ops->bufdata->end_remainder = spec_ops->opt->buf_num_elems % spec_ops->opt->stream_multiply;
  spec_ops->bufdata->start_remainder = 0;

  /* Formatting loop	*/
  for(i=0;i<spec_ops->opt->stream_multiply;i++)
  {
    spec_ops->td[i].buf = buf;
    spec_ops->td[i].seq = i;
    spec_ops->td[i].spec_ops = spec_ops;
    spec_ops->td[i].status = THREADSTATUS_KEEP_RUNNING;
    err = pthread_create(&spec_ops->td[i].pt, NULL, threaded_multistream, (void*)&spec_ops->td[i]);
    if(err != 0){
      E("Error in creating receiving thread");
      return -1;
    }
    spec_ops->bufdata->initialized = 1;
  }

  /* Mainloop		*/
  while(get_status_from_opt(spec_ops->opt) & STATUS_RUNNING && spec_ops->bufdata->active_threads > 0)
  {
    D("Starting main loop");
    pthread_mutex_lock(&spec_ops->bufdata->mainthread_mutex);
    while(spec_ops->bufdata->threads_ready_or_err < spec_ops->bufdata->active_threads)
      pthread_cond_wait(&spec_ops->bufdata->mainthread_cond, &spec_ops->bufdata->mainthread_mutex);
    pthread_mutex_unlock(&spec_ops->bufdata->mainthread_mutex);

    D("All threads ready");
    for(i=0;i<spec_ops->opt->stream_multiply;i++)
    {
      spec_ops->total_transacted_bytes += spec_ops->td[i].bytes_received;
      *buf_incrementer += spec_ops->td[i].bytes_received;
      D("Thread %d has written %ld bytes",, spec_ops->td[i].seq, spec_ops->td[i].bytes_received);
      spec_ops->td[i].bytes_received=0;
      if(spec_ops->td[i].status & THREADSTATUS_CLEAN_EXIT){
	spec_ops->bufdata->active_threads--;
	pthread_join(spec_ops->td[i].pt, NULL);
	spec_ops->td[i].pt = 0;
	D("One thread had clean exit");
      }
      if(spec_ops->td[i].status & THREADSTATUS_ERROR){
	D("One thread ended in err. Stopping everything");
	spec_ops->bufdata->active_threads = 0;
	errorflag=1;
	pthread_join(spec_ops->td[i].pt, NULL);
	spec_ops->td[i].pt = 0;
	break;
      }
    }
    if(errorflag ==1 || spec_ops->bufdata->active_threads ==0 || *buf_incrementer != CALC_BUFSIZE_FROM_OPT(spec_ops->opt) ){
      D("Breaking out of main loop");
      break;
    }

    spec_ops->bufdata->start_remainder = spec_ops->bufdata->end_remainder;
    spec_ops->bufdata->end_remainder = (spec_ops->opt->buf_num_elems-(spec_ops->opt->stream_multiply-spec_ops->bufdata->end_remainder)) % spec_ops->opt->stream_multiply;

    D("Chancing buffer. spec_ops->bufdata->start_remainder %d spec_ops->bufdata->end_remainder %d",, spec_ops->bufdata->start_remainder, spec_ops->bufdata->end_remainder);
    buf = change_buffer(se, &buf_incrementer);
    if(buf == NULL){
      E("Could not get a buffer");
      break;
    }

    if(err != 0)
      break;

    pthread_mutex_lock(&spec_ops->bufdata->recv_mutex);
    for(i=0;i<spec_ops->opt->stream_multiply;i++)
    {
      spec_ops->td[i].buf = buf;
      /* Clears the full flag	*/
      if(spec_ops->td[i].status & THREADSTATUS_KEEP_RUNNING)
	spec_ops->td[i].status = THREADSTATUS_KEEP_RUNNING;
    }
    D("Broadcasting for spec_ops->td to continue");
    pthread_cond_broadcast(&spec_ops->bufdata->recv_cond);
    spec_ops->bufdata->threads_ready_or_err=0;
    pthread_mutex_unlock(&spec_ops->bufdata->recv_mutex);
    D("Buffer complete in multithreaded");
  }
  D("Entering double rundown loop");
  pthread_mutex_lock(&spec_ops->bufdata->recv_mutex);
  for(i=0;i<spec_ops->opt->stream_multiply;i++)
  {
    *buf_incrementer += spec_ops->td[i].bytes_received;
    spec_ops->td[i].status = 0;
    if(spec_ops->td[i].fd != 0)
    {
      shutdown(spec_ops->td[i].fd, SHUT_RD);
      close(spec_ops->td[i].fd);
      spec_ops->td[i].fd =0;
    }
  }
  pthread_cond_broadcast(&spec_ops->bufdata->recv_cond);
  pthread_mutex_unlock(&spec_ops->bufdata->recv_mutex);
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
/*
 * Singlethreaded version of multistream. Not tested or developed
 *
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
  */
int tcp_multistreamsendcmd(struct streamer_entity* se, struct sender_tracking *st)
{
  struct socketopts* spec_ops = (struct socketopts*)se->opt;
  int err,i, errorflag=0;
  if(spec_ops->bufdata->initialized == 0)
  {
    spec_ops->bufdata->st = st;
    for(i=0;i<spec_ops->opt->stream_multiply;i++)
    {
      spec_ops->td[i].seq = i;
      spec_ops->td[i].spec_ops = spec_ops;
      spec_ops->td[i].status = THREADSTATUS_KEEP_RUNNING|THREADSTATUS_ALL_FULL;
      err = pthread_create(&spec_ops->td[i].pt, NULL, threaded_multistream, (void*)&spec_ops->td[i]);
      if(err != 0){
	E("Error in creating receiving thread");
	return -1;
      }
    }
    spec_ops->bufdata->initialized = 1;
  }

  spec_ops->bufdata->start_remainder = spec_ops->bufdata->end_remainder;
  spec_ops->bufdata->end_remainder = ((st->packetcounter/spec_ops->opt->packet_size)-(spec_ops->opt->stream_multiply-spec_ops->bufdata->end_remainder)) % spec_ops->opt->stream_multiply;

  pthread_mutex_lock(&spec_ops->bufdata->recv_mutex);
  for(i=0;i<spec_ops->opt->stream_multiply;i++)
  {
      spec_ops->td[i].buf = se->be->buffer;
      /* Clears the full flag	*/
      if(spec_ops->td[i].status & THREADSTATUS_KEEP_RUNNING)
	spec_ops->td[i].status = THREADSTATUS_KEEP_RUNNING;
  }
  D("Broadcasting for spec_ops->td to continue");
  pthread_cond_broadcast(&spec_ops->bufdata->recv_cond);
  spec_ops->bufdata->threads_ready_or_err=0;
  pthread_mutex_unlock(&spec_ops->bufdata->recv_mutex);

  pthread_mutex_lock(&spec_ops->bufdata->mainthread_mutex);
  while(spec_ops->bufdata->threads_ready_or_err < spec_ops->bufdata->active_threads)
    pthread_cond_wait(&spec_ops->bufdata->mainthread_cond, &spec_ops->bufdata->mainthread_mutex);
  pthread_mutex_unlock(&spec_ops->bufdata->mainthread_mutex);

  D("All threads ready");
  for(i=0;i<spec_ops->opt->stream_multiply;i++)
  {
    spec_ops->total_transacted_bytes +=spec_ops->td[i].bytes_received;
      st->packetcounter -= spec_ops->td[i].bytes_received;
    *(spec_ops->inc) += spec_ops->td[i].bytes_received;
    //spec_ops->total_transacted_bytes += spec_ops->td[i].bytes_received;
    //*buf_incrementer += spec_ops->td[i].bytes_received;
    D("Thread %d has written %ld bytes",, spec_ops->td[i].seq, spec_ops->td[i].bytes_received);
    spec_ops->td[i].bytes_received=0;
    if(spec_ops->td[i].status & THREADSTATUS_CLEAN_EXIT){
      spec_ops->bufdata->active_threads--;
      pthread_join(spec_ops->td[i].pt, NULL);
      spec_ops->td[i].pt = 0;
      D("One thread had clean exit");
    }
    if(spec_ops->td[i].status & THREADSTATUS_ERROR){
      D("One thread ended in err. Stopping everything");
      spec_ops->bufdata->active_threads = 0;
      errorflag=1;
      pthread_join(spec_ops->td[i].pt, NULL);
      spec_ops->td[i].pt = 0;
      break;
    }
  }
  if(errorflag ==1){
    E("Errorflag was raised");
    return -1;
  }
  /*
  else  if(spec_ops->bufdata->active_threads ==0)
    return 0;
    */
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
    spec_ops->total_transacted_bytes +=err;
    st->packetcounter -= err;
    *(spec_ops->inc) += err;
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
    else if(spec_ops->opt->optbits & CAPTURE_W_MULTISTREAM)
    {
      for(i=0;i<spec_ops->opt->stream_multiply  && spec_ops->fd > 0;i++)
      {
	if((spec_ops->td[i].fd = accept(spec_ops->fd, (struct sockaddr*)&(spec_ops->sin), &(spec_ops->sin_l))) < 0)
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
      err = generic_sendloop(se, 0, tcp_multistreamsendcmd, bboundary_bytenum);
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
  if(spec_ops->opt->optbits & CAPTURE_W_MULTISTREAM)
  {
    if(spec_ops->bufdata != NULL && spec_ops->bufdata->initialized == 1)
    {
      /*  Recv and send a bit different so this is a bit messy	*/
      if(spec_ops->opt->optbits & READMODE)
      {
	/* Double rundown loop	*/
	pthread_mutex_lock(&spec_ops->bufdata->recv_mutex);
	for(i=0;i<spec_ops->opt->stream_multiply;i++)
	{
	  spec_ops->td[i].status = 0;
	  if(spec_ops->td[i].fd != 0)
	  {
	    shutdown(spec_ops->td[i].fd, SHUT_RD);
	    close(spec_ops->td[i].fd);
	  }
	}
	pthread_cond_broadcast(&spec_ops->bufdata->recv_cond);
	pthread_mutex_unlock(&spec_ops->bufdata->recv_mutex);
      }
      for(i=0;i<spec_ops->opt->stream_multiply;i++)
      {
	if(spec_ops->td[i].pt != 0)
	  pthread_join(spec_ops->td[i].pt, NULL);
      }

    }
    pthread_mutex_destroy(&spec_ops->bufdata->recv_mutex);
    pthread_mutex_destroy(&spec_ops->bufdata->mainthread_mutex);
    pthread_cond_destroy(&spec_ops->bufdata->recv_cond);
    pthread_cond_destroy(&spec_ops->bufdata->mainthread_cond);
  }
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
