#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "common_filehandling.h"

extern FILE* logfile;
#define SKIP_LOADED 	1
#define SKIP_SENT 	2
void skip_missing(struct opt_s* opt, struct sender_tracking* st, int lors)
{
  unsigned long * target = NULL;
  if(lors == SKIP_LOADED)
    target = &(st->files_loaded);
  else if(lors == SKIP_SENT)
    target = &(st->files_sent);

  if(target != NULL)
  {
    while(*target <= st->n_files_probed && opt->fi->files[*target].status & FH_MISSING){
      long nuf = MIN((opt->fi->n_files - st->packets_loaded), ((unsigned long)opt->buf_num_elems));

      D("Skipping a file, fileholder set to FH_MISSING for file %lu",, st->files_loaded);

      /* files skipped is just for statistics so don't want to log it twice	*/
      if(lors == SKIP_SENT)
	st->files_skipped++;
      //st->files_loaded++;
      (*target)++;

      if(lors == SKIP_SENT)
	st->packets_sent += nuf;
      else if(lors == SKIP_LOADED)
	st->packets_loaded+= nuf;
    }
  }
  else
    E("Weird target");
}

int start_loading(struct opt_s * opt, struct buffer_entity *be, struct sender_tracking *st)
{
  skip_missing(opt,st,SKIP_LOADED);
  long nuf = MIN((st->n_packets_probed - st->packets_loaded), ((unsigned long)opt->buf_num_elems));
  int err;
  D("Loading: %lu, packets loaded is %lu, packet probed %ld",, nuf, st->packets_loaded, st->n_packets_probed);
  if(st->files_loaded == st->n_files_probed){
    D("Loaded up to n_files!");
    return DONTRYLOADNOMORE;
  }
  FI_READLOCK(opt->fi);
  if(!(FH_STATUS(st->files_loaded) & FH_ONDISK)){
    if(FH_STATUS(st->files_loaded) & (FH_BUSY|FH_INMEM))
    {
      if(st->files_in_loading > 0){
	FIUNLOCK(opt->fi);
	return DONTRYLOADNOMORE;
      }
      else
      {
	while(FH_STATUS(st->files_loaded) & (FH_BUSY|FH_INMEM))
	{
	  err = mutex_free_wait_on_update(opt->fi);
	  CHECK_ERR("wait on update");
	}
      }
      //TODO add a wait here 
    }
    else{
      D("Not on disk so not loading");
      FIUNLOCK(opt->fi);
      return DONTRYLOADNOMORE;
    }
  }
  FIUNLOCK(opt->fi);
  /*TODO Lets skip this perf improvement stuff for now */
  /*
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

    */
  /* TODO: Not checking if FH_ONDISK is set */
  D("Requested a load start on file %lu",, st->files_loaded);
  if (be == NULL){
    if(check_if_free(opt->membranch) != 0){
      D("No more free buffers");
      if(st->files_in_loading != 0){
	D("wont loadup since no more free buffers and we have files in loading");
	return DONTRYLOADNOMORE;
      }
    }
    be = get_free(opt->membranch, opt, (void*)(&(st->files_loaded)), NULL,1);
    st->allocated_to_load--;
  }
  /* Reacquiring just updates the file number we want */
  else
  {
    be->acquire((void*)be, opt, &(st->files_loaded));
  }
  CHECK_AND_EXIT(be);

  st->files_in_loading++;
  D("Setting seqnum %lu to load %lu packets",,st->files_loaded, nuf);

  LOCK(be->headlock);
  long *inc;
  be->simple_get_writebuf(be, &inc);
  /* Why the hell did i do this? :D */
  /*
  if(nuf == opt->buf_num_elems)
    *inc = FILESIZE;
  else
  */
  /*
  if(nuf == opt->buf_num_elems){
    *inc = FILESIZE;
  }
  else{
    D("Loading incomplete file so setting inc not to whole file");
    *inc = nuf*opt->packet_size;
  }
  */
  *inc = nuf*(opt->packet_size);

  be->set_ready(be, 1);
  pthread_cond_signal(be->iosignal);
  UNLOCK(be->headlock);
  D("Loading request complete for id %lu",, st->files_loaded);

  st->packets_loaded+=nuf;
  st->files_loaded++;
  return 0;
}
/* Starts loading st.allocated_to_load and returns */
/* number loaded */
int loadup_n(struct opt_s *opt, struct sender_tracking * st)
{
  int i, err;

  int loadup = MIN(st->allocated_to_load, opt->n_threads);
  /*
  for(i=0;i<loadup;i++)
  {
    if(FH_STATUS(st->files_loaded) & FH_BUSY)
  }
  FIUNLOCK(opt->fi);
  */

  /* Check if theres empties right at the start */
  /* Added && for files might be skipped in start_loading */
  for(i=0;i<loadup;i++){
    /* When running in sendmode, the buffers are first getted 	*/
    /* and then signalled to start loading packets from the hd 	*/
    /* A ready loaded buffer is getted by running get_loaded	*/
    /* With a matching sequence number				*/
    err = start_loading(opt, NULL, st);
    if(err == DONTRYLOADNOMORE)
      break;
    CHECK_ERR("Start loading");
    /* TODO: loadup might cut short if we're sending a recording that has just started */
  }
  return 0;
}
void init_sender_tracking(struct opt_s *opt, struct sender_tracking *st)
{
  memset(st, 0,sizeof(struct sender_tracking));

#if!(PREEMPTKERNEL)
  //TIMERTYPE onenano;
  ZEROTIME(st->onenano);
  SETONE(st->onenano);
#endif
  ZEROTIME(st->req);
  full_metadata_update(opt->fi, &st->n_files_probed, &st->n_packets_probed, &st->status_probed);
}
inline int should_i_be_running(struct opt_s *opt, struct sender_tracking *st)
{
  //if(!spec_ops->running)
  if(!(opt->status & STATUS_RUNNING)){
    D("Run status set off!");
    return 0;
  }
  /* Check if the receiver is still running */
  /*
  if(opt->optbits & READMODE && (st->status_probed & FILESTATUS_RECORDING)){
    return 1;
  }
  */
  if(st->n_packets_probed > st->packets_sent)
    return 1;
  /* If we still have files */
  if(st->files_sent != st->n_files_probed){
    return 1;
  }
  /* If there's still packets to be sent */

  D("Shouldnt be running anymore");
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
    if(st->allocated_to_load <5)
      st->allocated_to_load =5;
    D("rate as %d ns. Setting to use max %d buffers",, opt->wait_nanoseconds, st->allocated_to_load);
  }
}
/* TODO: This logic needs to be rethunked! Lots of cases to consider with live sending */
int jump_to_next_file(struct opt_s *opt, struct streamer_entity *se, struct sender_tracking *st)
{
  int err;
  full_metadata_update(opt->fi, &st->n_files_probed, &st->n_packets_probed, &st->status_probed);
  st->total_bytes_to_send = st->n_packets_probed*spec_ops->opt->packet_size;
  //st->n_files_probed = get_n_files(opt->fi);
  //st->status_probed = get_status(opt->fi);
  if(se->be != NULL){
    D("Buffer empty for: %lu",, st->files_sent);
    st->files_sent++;
    /* Not too efficient to free and then get a new, but doing this for simpler logic	 */
    D("Freeing used buffer for other use");
    set_free(opt->membranch, se->be->self);
    se->be = NULL;
    st->allocated_to_load++;
  }
  while(st->files_sent == st->n_files_probed){
    if((st->status_probed = get_status(opt->fi)) & FILESTATUS_RECORDING){
      D("All sent with %ld packets, but we're still recording on %s",,st->packets_sent, opt->filename);	
      err = wait_on_update(opt->fi);
      CHECK_ERR("wait on update");
      full_metadata_update(opt->fi, &st->n_files_probed, &st->n_packets_probed, &st->status_probed);
  st->total_bytes_to_send = st->n_packets_probed*spec_ops->opt->packet_size;
    }
    else{
      return ALL_DONE;
    }
  }
  //full_metadata_update(opt->fi, &st->n_files_probed, &st->n_packets_probed, &st->status_probed);
  //st->n_packets_probed = get_n_packets(opt->fi);
  //st->status_probed = get_status(opt->fi);
  /* -1 here, since indexes start at 0 */
  while(st->files_loaded < st->n_files_probed && st->allocated_to_load > 0){
    D("Still files to be loaded. Loading %lu. Allocated %d",, st->files_loaded, st->allocated_to_load);
    /* start_loading increments files_loaded */
    err = start_loading(opt, se->be, st);
    if(err == DONTRYLOADNOMORE){
      D("Loader signalled to stop loading");
      break;
    }
    CHECK_ERR("Loading file");
  }
  if(st->files_in_loading == 0){
    D("Can't start loading and can't go to wait for loading files on %s. Waiting and running again",, opt->filename);
    err = wait_on_update(opt->fi);
    CHECK_ERR("wait on update");
    return jump_to_next_file(opt, se, st);
  }

  //se->be = NULL;
  while(se->be == NULL){
    D("Getting new loaded for file %lu, filename %s",, st->files_sent, opt->filename);
    FI_READLOCK(opt->fi);
    skip_missing(opt,st,SKIP_SENT);
    FIUNLOCK(opt->fi);
    /* Skip this optimization for now. TODO: reimplement */
    /*
       if (opt->fileholders->status & FH_INMEM){
       D("File should still be in mem..");
       if ((se->be = get_lingering(opt->membranch, opt, opt->fileholders, 0)) == NULL){
       D("Lingering copy has disappeared! Requesting load on file");
       struct fileholder* tempfh = (struct fileholder*)malloc(sizeof(struct fileholder));
       memcpy(tempfh, opt->fileholders, sizeof(struct fileholder));
       tempfh->next = NULL;
       tempfh->status &= ~FH_INMEM;
       if(st->head_loaded != NULL){
       tempfh->next = st->head_loaded;
       st->head_loaded = tempfh;
       }
       else
       st->head_loaded = tempfh;
       return jump_to_next_file(opt, se, st);
       }
       else
       D("Got lingering loaded file %lu to send.",, opt->fileholders->id);
       }
       else
       */
    if(st->files_in_loading > 0)
    {
      D("File should be waiting for us now or soon with status %lu. in loading %ld",, st->files_sent, st->files_in_loading);
      se->be = get_loaded(opt->membranch, st->files_sent, opt);
      //buf = se->be->simple_get_writebuf(se->be, &inc);
      if(se->be != NULL){
	D("Got loaded file %lu to send.",, st->files_sent);
	st->files_in_loading--;
      }
      else{
	E("Couldnt get loaded file");
	return -1;
      }
    }
    else{
      E("No files in loading so shouldn't get here. Loaded %ld, sent %ld, in loading: %ld. Filename %s ",, st->files_loaded, st->files_sent,st->files_in_loading, opt->filename);
      return -1;
    }
  }
  return 0;
}
void init_resq(struct resq_info* resq)
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
