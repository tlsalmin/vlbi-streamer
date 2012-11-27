#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "common_filehandling.h"

extern FILE* logfile;

int start_loading(struct opt_s * opt, struct buffer_entity *be, struct sender_tracking *st)
{
  AUGMENTLOCK;
  long nuf = MIN((*opt->total_packets - st->packets_loaded), ((unsigned long)opt->buf_num_elems));
  D("Total packets is: %lu, packets loaded is %lu",, *opt->total_packets, st->packets_loaded);
  /* Add spinlocks here so broken recers get info set here before use */
  //while(opt->fileholders[st->files_loaded]  == -1 && st->files_loaded <= *opt->cumul){
  while(st->head_loaded != NULL && st->head_loaded->status == FH_MISSING){
    D("Skipping a file, fileholder set to FH_MISSING for file %lu",, st->head_loaded->id);
    st->head_loaded = st->head_loaded->next;
    //st->files_loaded++;
    st->files_skipped++;
    //spec_ops->files_sent++;
    /* If last file is missing, we might hit negative on left_to_send 	*/
    /*
    if((*opt->total_packets - st->packets_sent) < (unsigned long)opt->buf_num_elems){
      st->packets_loaded=*opt->total_packets;
      st->packets_sent += nuf;
    }
    else{
    */
      st->packets_sent += nuf;
      st->packets_loaded+= nuf;
    //}
    /* Special case where last file is missing */
    if(st->head_loaded->next == NULL)
      return 0;
    else
      st->head_loaded = st->head_loaded->next;
    /*
    if(st->files_loaded == *opt->cumul)
      return 0;
      */
  }
  if(st->head_loaded->status & FH_INMEM){
    struct buffer_entity *tempen;
    AUGMENTUNLOCK;
    if((tempen = get_lingering(opt->membranch, opt, st->head_loaded, 0)) != NULL){
      D("Found lingering file!");
      if(be != NULL){
	D("Had a ready buffer, but found lingering. Setting old to free");
	set_free(opt->membranch, be->self);
      }
      be = tempen;
      st->packets_loaded+=nuf;
      st->head_loaded = st->head_loaded->next;
      return 0;
    }
    else
      D("Didn't find lingering file");
  }
  else
    AUGMENTUNLOCK;

  /* TODO: Not checking if FH_ONDISK is set */
  D("Requested a load start on file %lu",, st->head_loaded->id);
  assert(st->head_loaded->status & FH_ONDISK);
  if (be == NULL){
    be = get_free(opt->membranch, opt, ((void*)(st->head_loaded)), NULL);
    st->allocated_to_load--;
    CHECK_ERR_NONNULL(be, "Get loadable");
  }
  /* Reacquiring just updates the file number we want */
  else
    //be->acquire((void*)be, opt, st->head_loaded->id,0);
    be->acquire((void*)be, opt, st->head_loaded);
  CHECK_AND_EXIT(be);
  D("Setting seqnum %lu to load %lu packets",,st->head_loaded->id, nuf);
  LOCK(be->headlock);
  //D("HUR");
  long *inc;
  be->simple_get_writebuf(be, &inc);
  if(nuf == opt->buf_num_elems)
    *inc = FILESIZE;
  else
    *inc = nuf*opt->packet_size;

  st->packets_loaded+=nuf;
  be->set_ready(be);
  pthread_cond_signal(be->iosignal);
  UNLOCK(be->headlock);
  D("Loading request complete for id %lu",, st->head_loaded->id);

  st->head_loaded = st->head_loaded->next;
  return 0;
}
/* Starts loading st.allocated_to_load and returns */
/* number loaded */
int loadup_n(struct opt_s *opt, struct sender_tracking * st){
  int i, err;
  AUGMENTLOCK;
  int cumulpeek = (*opt->cumul);
  //packetpeek = (*opt->total_packets);
  AUGMENTUNLOCK;
  int loadup = MIN(st->allocated_to_load, cumulpeek);
  loadup = MIN(loadup, opt->n_threads);

  /* Check if theres empties right at the start */
  /* Added && for files might be skipped in start_loading */
  for(i=0;i<loadup && st->head_loaded != NULL;i++){
    /* When running in sendmode, the buffers are first getted 	*/
    /* and then signalled to start loading packets from the hd 	*/
    /* A ready loaded buffer is getted by running get_loaded	*/
    /* With a matching sequence number				*/
    err = start_loading(opt, NULL, st);
    CHECK_ERR("Start loading");
    /*
    SAUGMENTLOCK;
    cumulpeek = (*spec_ops->opt->cumul);
    packetpeek = (*spec_ops->opt->total_packets);
    SAUGMENTUNLOCK;
    */
    /* TODO: loadup might cut short if we're sending a recording that has just started */
  }
  return (i-1);
}
void init_sender_tracking(struct opt_s *opt, struct sender_tracking *st){
  memset(st, 0,sizeof(struct sender_tracking));
  st->packets_sent = st->packets_loaded = 0;//spec_ops->opt->total_packets;
#ifdef UGLY_BUSYLOOP_ON_TIMER
  //TIMERTYPE onenano;
  ZEROTIME(st->onenano);
  SETONE(st->onenano);
#endif
  ZEROTIME(st->req);
  AUGMENTLOCK;
  //pthread_spin_lock(opt->augmentlock);
  st->head_loaded = opt->fileholders;
  AUGMENTUNLOCK;
  //pthread_spin_unlock(opt->augmentlock);
}
inline int should_i_be_running(struct opt_s *opt, struct sender_tracking *st){
  /* TODO: Proper checking for live sending/receiving */
  //unsigned long cumulpeek;
  //if(!spec_ops->running)
  if(!(opt->status & STATUS_RUNNING))
    return 0;
  /*
  SAUGMENTLOCK;
  cumulpeek = *(spec_ops->opt->cumul);
  SAUGMENTUNLOCK;
  */
  /* Check if the receiver is still running */
  if(opt->optbits & LIVE_SENDING && opt->liveother != NULL){
    if(opt->liveother->status & STATUS_RUNNING)
      return 1;
  }
  /* If we still have files */
  if(opt->fileholders != NULL){
    return 1;
  }
  /* If there's still packets to be sent */
  if(*(opt->total_packets) > st->packets_sent)
    return 1;
  
  return 0;
}
void throttling_count(struct opt_s* opt, struct sender_tracking * st)
{
  if(opt->wait_nanoseconds == 0){
    st->allocated_to_load = MIN(TOTAL_MAX_DRIVES_IN_USE, opt->n_threads);
    D("No wait set, so setting to use %d buffers",, st->allocated_to_load);
  }
  else
  {
    long rate_in_bytes = (BILLION/((long)opt->wait_nanoseconds))*opt->packet_size;
    /* Add one as n loading for speed and one is being sent over the network */
    st->allocated_to_load = MIN(TOTAL_MAX_DRIVES_IN_USE, rate_in_bytes/(MBYTES_PER_DRIVE*MILLION) + 1);
    D("rate as %d ns. Setting to use max %d buffers",, opt->wait_nanoseconds, st->allocated_to_load);
  }
}
int jump_to_next_file(struct opt_s *opt, struct streamer_entity *se, struct sender_tracking *st){
  struct fileholder * tempfh;
  int err;
  //long cumulpeek; //= (*opt->cumul);
  //st->files_sent++;
  D("Buffer empty for: %lu",, opt->fileholders->id);
  AUGMENTLOCK;
  tempfh = opt->fileholders;
  opt->fileholders = opt->fileholders->next;
  free(tempfh);
  st->packetpeek = *(opt->total_packets);
  /* Check for missing file here so we can keep simplebuffer simple 	*/
  //packetpeek = (*opt->total_packets);
  AUGMENTUNLOCK;
  //if(st->files_loaded < cumulpeek){
  if(st->head_loaded != NULL){
    D("Still files to be loaded. Loading %lu",, st->head_loaded->id);
    /* start_loading increments files_loaded */
    err = start_loading(opt, se->be, st);
    CHECK_ERR("Loading file");
    if(st->allocated_to_load > 0 && st->head_loaded != NULL){
      err = start_loading(opt, NULL, st);
      CHECK_ERR("Loading more files");
    }
  }
  else{
    D("Loaded enough files. Setting memorybuf to free");
    set_free(opt->membranch, se->be->self);
    st->allocated_to_load++;
  }

  /*
     while(st->files_sent < opt->cumul && opt->fileholders[st->files_sent] == -1)
     st->files_sent++;
     */
  se->be = NULL;
  while(se->be == NULL){
    //AUGMENTLOCK;
    //cumulpeek = (*opt->cumul);
    //AUGMENTUNLOCK;
    //if(st->files_sent < cumulpeek){
    if(opt->fileholders != NULL){
      D("Getting new loaded for file %lu",, opt->fileholders->id);
      if(opt->fileholders != NULL && opt->fileholders->status == FH_MISSING){
	D("Skipping a file, fileholder set to -1 for file %lu",, st->head_loaded->id);
	AUGMENTLOCK;
	tempfh = opt->fileholders;
	opt->fileholders = opt->fileholders->next;
	free(tempfh);
	AUGMENTUNLOCK;
      }
      else
      {
	se->be = get_loaded(opt->membranch, opt->fileholders->id, opt);
	//buf = se->be->simple_get_writebuf(se->be, &inc);
	D("Got loaded file %lu to send.",, opt->fileholders->id);
	//CHECK_AND_EXIT(se->be);
      }
    }
    else{
      //E("Shouldn't be here since all packets have been sent!");
      D("All files sent! Time to wrap it up");
      //set_free(opt->membranch, se->be->self);
      return ALL_DONE;
    }
  }
  return 0;
  }
