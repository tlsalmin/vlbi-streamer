#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "common_filehandling.h"

extern FILE* logfile;
#define SKIP_LOADED 	1
#define SKIP_SENT 	2
inline void skip_missing(struct opt_s* opt, struct sender_tracking* st, int lors)
{
  unsigned long * target;
  if(lors == SKIP_LOADED)
    target = &(st->files_loaded);
  else if(lors == SKIP_SENT)
    target = &(st->files_sent);

  while(*target <= opt->fi->n_files && opt->fi->files[*target].status & FH_MISSING){
    long nuf = MIN((get_n_packets(opt->fi) - st->packets_loaded), ((unsigned long)opt->buf_num_elems));

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

int start_loading(struct opt_s * opt, struct buffer_entity *be, struct sender_tracking *st)
{
  long nuf = MIN((get_n_packets(opt->fi) - st->packets_loaded), ((unsigned long)opt->buf_num_elems));
  D("Loading: %lu, packets loaded is %lu",, nuf, st->packets_loaded);
  FILOCK(opt->fi);
  skip_missing(opt,st,SKIP_LOADED);
  if(st->files_loaded == opt->fi->n_files){
    D("Loaded up to n_files!");
    FIUNLOCK(opt->fi);
    return DONTRYLOADNOMORE;
  }
  if(!(FH_STATUS(st->files_loaded) & FH_ONDISK)){
    D("Not on disk so not loading");
    FIUNLOCK(opt->fi);
    return DONTRYLOADNOMORE;
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
    be = get_free(opt->membranch, opt, (void*)(&(st->files_loaded)), NULL);
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
  *inc = nuf*opt->packet_size;

  be->set_ready(be);
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
  //FILOCK(opt->fi);

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
    /*
       SAUGMENTLOCK;
       cumulpeek = (*spec_ops->opt->cumul);
       packetpeek = (*spec_ops->opt->total_packets);
       SAUGMENTUNLOCK;
       */
    /* TODO: loadup might cut short if we're sending a recording that has just started */
  }
  return 0;
}
void init_sender_tracking(struct opt_s *opt, struct sender_tracking *st)
{
  (void)opt;
  memset(st, 0,sizeof(struct sender_tracking));

#ifdef UGLY_BUSYLOOP_ON_TIMER
  //TIMERTYPE onenano;
  ZEROTIME(st->onenano);
  SETONE(st->onenano);
#endif
  ZEROTIME(st->req);
}
inline int should_i_be_running(struct opt_s *opt, struct sender_tracking *st)
{
  //if(!spec_ops->running)
  if(!(opt->status & STATUS_RUNNING))
    return 0;
  /* Check if the receiver is still running */
  if(opt->optbits & READMODE && get_status(opt->fi) & FILESTATUS_RECORDING){
    return 1;
  }
  /* If we still have files */
  if(st->files_sent != get_n_files(opt->fi)){
    return 1;
  }
  /* If there's still packets to be sent */
  if(get_n_packets(opt->fi) > st->packets_sent)
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
int jump_to_next_file(struct opt_s *opt, struct streamer_entity *se, struct sender_tracking *st)
{
  int err;
  //long cumulpeek; //= (*opt->cumul);
  //st->files_sent++;
  if(se->be != NULL){
    D("Buffer empty for: %lu",, st->files_sent);
    st->files_sent++;
  }
  if(st->files_loaded < get_n_files(opt->fi)){
    D("Still files to be loaded. Loading %lu",, st->files_loaded);
    /* start_loading increments files_loaded */
    err = start_loading(opt, se->be, st);
    CHECK_ERR("Loading file");
    while(st->allocated_to_load > 0)
    {
      err = start_loading(opt, NULL, st);
      if(err == DONTRYLOADNOMORE)
	break;
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
    D("Getting new loaded for file %lu",, st->files_sent);
    skip_missing(opt,st,SKIP_SENT);
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
      D("File should be waiting for us now or soon with status %lu",, st->files_sent);
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
      //CHECK_AND_EXIT(se->be);
    }
  }
return 0;
}
