#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "dummy_stream.h"
#include "common_filehandling.h"
#include "udp_stream.h"
#include "sockethandling.h"

/*
#ifdef UDPS_EXIT
#undef UDPS_EXIT
#define UDPS_EXIT do {D("UDP_STREAMER: Closing sender thread. Total sent %lu, Supposed to send: %lu",, st.packets_sent, spec_ops->opt->total_packets); if(se->be != NULL){set_free(spec_ops->opt->membranch, se->be->self);} set_status_for_opt(spec_ops->opt,STATUS_STOPPED);if(spec_ops->fd != 0)pthread_exit(NULL);}while(0)
#endif
*/
int setup_dummy_socket(struct opt_s *opt, struct streamer_entity *se)
{
  struct socketopts *spec_ops =(struct socketopts *) malloc(sizeof(struct socketopts));
  CHECK_ERR_NONNULL(spec_ops, "spec ops malloc");
  //spec_ops->running = 1;
  se->opt = (void*)spec_ops;

  spec_ops->opt = opt;
  
  return 0;
}
void get_dummy_stats(void *opt, void *stats)
{
  struct stats *stat = (struct stats * ) stats;
  struct socketopts *spec_ops = (struct socketopts*)opt;
  stat->total_packets += spec_ops->opt->total_packets;
  stat->total_bytes += spec_ops->total_transacted_bytes;
  stat->incomplete += spec_ops->incomplete;
  stat->dropped += spec_ops->missing;
}
int close_dummy_streamer(struct streamer_entity *se,void *stats)
{
  struct socketopts *spec_ops = (struct socketopts *)se->opt;
  get_dummy_stats(se->opt,  stats);
  LOG("DUMMY_STREAM: Closed\n");
  free(spec_ops);
  return 0;
}
void dummy_stop(struct streamer_entity *se)
{
  set_status_for_opt((((struct socketopts*)se->opt)->opt),STATUS_STOPPED);
}
void dummy_init_default(struct opt_s *opt, struct streamer_entity *se)
{
  (void)opt;
  se->init = setup_dummy_socket;
  se->close = close_dummy_streamer;
  se->get_stats = get_dummy_stats;
  se->close_socket = dummy_stop;
}
int dummy_init_dummy_receiver( struct opt_s *opt, struct streamer_entity *se)
{
  dummy_init_default(opt,se);
    se->start = dummy_receiver;
  se->stop = dummy_stop;
  return se->init(opt, se);
}
int dummy_init_dummy_sender( struct opt_s *opt, struct streamer_entity *se)
{
  dummy_init_default(opt,se);
  se->start = dummy_sender;
  se->stop = dummy_stop;
  return se->init(opt, se);
}
int dummy_sendcmd(struct streamer_entity*se, struct sender_tracking *st)
{
  struct socketopts *spec_ops = se->opt;
  st->packets_sent++;
  spec_ops->total_transacted_bytes +=spec_ops->opt->packet_size;
  *(spec_ops->inc)+= spec_ops->opt->packet_size;
  st->packetcounter--;
  return 0;
}
void * dummy_sender(void * streamo)
{
  generic_sendloop((struct streamer_entity*) streamo, 1,dummy_sendcmd, bboundary_packetnum);
  pthread_exit(NULL);
}
void * dummy_receiver(void *streamo)
{
  int err = 0;
  struct streamer_entity *se =(struct streamer_entity*)streamo;
  struct socketopts *spec_ops = (struct socketopts *)se->opt;

  struct resq_info* resq = (struct resq_info*)malloc(sizeof(struct resq_info));
  memset(resq, 0, sizeof(struct resq_info));

  reset_udpopts_stats(spec_ops);

  se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch, spec_ops->opt,&(spec_ops->opt->cumul), NULL,1);
  CHECK_AND_EXIT(se->be);

  resq->buf = se->be->simple_get_writebuf(se->be, &resq->inc);
  /* IF we have packet resequencing	*/
  if(!(spec_ops->opt->optbits & DATATYPE_UNKNOWN))
    init_resq(resq);

  LOG("UDP_STREAMER: Starting stream capture\n");
  while(get_status_from_opt(spec_ops->opt) & STATUS_RUNNING){
    err = handle_buffer_switch(se, resq);
    if(err != 0){
      LOG("Error or done in buffer switch");
      set_status_for_opt(spec_ops->opt, STATUS_ERROR);
      break;
    }
    // RECEIVE
    usleep(5);
    err = spec_ops->opt->packet_size;
    err = udps_handle_received_packet(se, resq, err);
    if(err != 0){
      E("Error in receive packet. Closing!");
      set_status_for_opt(spec_ops->opt, STATUS_ERROR);
      break;
    }
  }
  D("Loop finished for receiving %s",, spec_ops->opt->filename);
  /* Release last used buffer */
  if(resq->before != NULL){
    *(resq->inc_before) = CALC_BUFSIZE_FROM_OPT(spec_ops->opt);
    free_the_buf(resq->before);
  }
  if(*(resq->inc) == 0)
    se->be->cancel_writebuf(se->be);
  else{
    if(spec_ops->opt->fi != NULL){
      unsigned long n_now = add_to_packets(spec_ops->opt->fi, resq->i);
      D("N packets is now %lu",, n_now);
    }

    se->be->set_ready_and_signal(se->be,0);
    spec_ops->opt->cumul++;
    /*
    LOCK(se->be->headlock);
    pthread_cond_signal(se->be->iosignal);
    UNLOCK(se->be->headlock);
    */
  }
  /* Set total captured packets as saveable. This should be changed to just */
  /* Use opts total packets anyway.. */
  //spec_ops->opt->total_packets = spec_ops->total_captured_packets;
  D("Saved %lu files and %lu packets for recname %s",, spec_ops->opt->cumul, spec_ops->opt->total_packets, spec_ops->opt->filename);
  LOG("UDP_STREAMER: Closing receiver thread\n");
  //spec_ops->running = 0;
  /* Main thread will free if we have a real datatype */
  if(spec_ops->opt->optbits & DATATYPE_UNKNOWN)
    free(resq);
  pthread_exit(NULL);
}
