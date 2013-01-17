#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "dummy_stream.h"
#include "common_filehandling.h"
#include "udp_stream.h"


int setup_dummy_socket(struct opt_s *opt, struct streamer_entity *se)
{
  struct udpopts *spec_ops =(struct udpopts *) malloc(sizeof(struct udpopts));
  CHECK_ERR_NONNULL(spec_ops, "spec ops malloc");
  //spec_ops->running = 1;
  se->opt = (void*)spec_ops;

  spec_ops->opt = opt;
  
  return 0;
}
void get_dummy_stats(void *opt, void *stats)
{
  struct stats *stat = (struct stats * ) stats;
  struct udpopts *spec_ops = (struct udpopts*)opt;
  stat->total_packets += *spec_ops->opt->total_packets;
  stat->total_bytes += spec_ops->total_captured_bytes;
  stat->incomplete += spec_ops->incomplete;
  stat->dropped += spec_ops->missing;
}
int close_dummy_streamer(void *opt_own,void *stats)
{
  struct udpopts *spec_ops = (struct udpopts *)opt_own;
  get_dummy_stats(opt_own,  stats);
  LOG("DUMMY_STREAM: Closed\n");
  free(spec_ops);
  return 0;
}
void dummy_stop(struct streamer_entity *se)
{
  ((struct udpopts*)se->opt)->opt->status = STATUS_STOPPED;
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
void * dummy_sender(void * streamo)
{
  int err = 0;
  long *inc, sentinc=0, packetcounter=0;
  struct streamer_entity *se =(struct streamer_entity*)streamo;
  struct udpopts *spec_ops = (struct udpopts *)se->opt;
  struct sender_tracking st;
  init_sender_tracking(spec_ops->opt, &st);
  throttling_count(spec_ops->opt, &st);
  se->be = NULL;
  spec_ops->total_captured_bytes = 0;
  spec_ops->total_captured_packets = 0;
  spec_ops->out_of_order = 0;
  spec_ops->incomplete = 0;
  spec_ops->missing = 0;
  D("Wait between is %d here",, spec_ops->opt->wait_nanoseconds);

  //void * buf = se->be->simple_get_writebuf(se->be, &inc);
  D("Getting first loaded buffer for sender");
  
  err = jump_to_next_file(spec_ops->opt, se, &st);
  if(err != 0){
    UDPS_EXIT;
  }


  CHECK_AND_EXIT(se->be);
  unsigned long minsleep = get_min_sleeptime();
  LOG("Can sleep max %lu microseconds on average\n", minsleep);

  se->be->simple_get_writebuf(se->be, &inc);

  D("Starting stream send");
  //i=0;
  GETTIME(spec_ops->opt->wait_last_sent);
  long packetpeek = get_n_packets(spec_ops->opt->fi);

  while(should_i_be_running(spec_ops->opt, &st) == 1){
    if(packetcounter == spec_ops->opt->buf_num_elems || (st.packets_sent - packetpeek  == 0))
    {
      D("Sent %lu packets for file %lu. Changing buffer",, packetcounter, st.files_sent);
      err = jump_to_next_file(spec_ops->opt, se, &st);
      if(err == ALL_DONE){
	D("All done for sending %s",, spec_ops->opt->filename);
	UDPS_EXIT;
	//break;
      }
      else if (err < 0){
	E("Error in getting buffer");
	UDPS_EXIT_ERROR;
	//break;
      }
      se->be->simple_get_writebuf(se->be, &inc);
      packetpeek = get_n_packets(spec_ops->opt->fi);
      packetcounter = 0;
      sentinc = 0;
      //i=0;
    }
    udps_wait_function(&st, spec_ops->opt);
    //PACKET SEND
    usleep(5);
    err = spec_ops->opt->packet_size;
    if(err < 0){
      perror("Send packet");
      UDPS_EXIT_ERROR;
      //break;
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
  if(se->be != NULL)
  {
    D("Still have buffer in se->be. It sent %lu before we quit",, packetcounter);
    //free_the_buf(se->be);
  }
  assert(st.packets_sent == get_n_packets(spec_ops->opt->fi));
  D("All done. Sent %lu packets for file %s",, st.packets_sent, spec_ops->opt->filename);
  UDPS_EXIT;
}
void * dummy_receiver(void *streamo)
{
  int err = 0;
  struct streamer_entity *se =(struct streamer_entity*)streamo;
  struct udpopts *spec_ops = (struct udpopts *)se->opt;

  struct resq_info* resq = (struct resq_info*)malloc(sizeof(struct resq_info));
  memset(resq, 0, sizeof(struct resq_info));

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
    init_resq(resq);

  LOG("UDP_STREAMER: Starting stream capture\n");
  while(spec_ops->opt->status & STATUS_RUNNING){
    err = handle_buffer_switch(se, resq);
    if(err != 0){
      LOG("Error or done in buffer switch");
      spec_ops->opt->status = STATUS_ERROR;
      break;
    }
    // RECEIVE
    usleep(5);
    err = spec_ops->opt->packet_size;
    err = udps_handle_received_packet(se, resq, err);
    if(err != 0){
      E("Error in receive packet. Closing!");
      spec_ops->opt->status = STATUS_ERROR;
      break;
    }
  }
  /* Release last used buffer */
  if(resq->before != NULL){
    //*(resq->inc_before) = spec_ops->opt->buf_num_elems;
    *(resq->inc_before) = spec_ops->opt->filesize;
    free_the_buf(resq->before);
  }
  if(*(resq->inc) == 0)
    se->be->cancel_writebuf(se->be);
  else{
    if(spec_ops->opt->fi != NULL){
      unsigned long n_now = add_to_packets(spec_ops->opt->fi, resq->i);
      D("N packets is now %lu",, n_now);
    }

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
  /* Main thread will free if we have a real datatype */
  if(spec_ops->opt->optbits & DATATYPE_UNKNOWN)
    free(resq);
  pthread_exit(NULL);
}
