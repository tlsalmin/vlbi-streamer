#include <stdio.h>
#include <malloc.h>
#include <sys/uio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include "config.h"
#include <string.h> /* For preheat */
#ifdef HAVE_HUGEPAGES
#include <sys/mman.h>
#endif
#include "resourcetree.h"
#include "simplebuffer.h"
#include "streamer.h"
#include "assert.h"
#ifndef MMAP_NOT_SHMGET
#include <sys/ipc.h>
#include <sys/shm.h>
#endif

#define HAVE_ASSERT 1
#define ASSERT(x) do{if(HAVE_ASSERT){assert(x);}}while(0)
#define CHECK_RECER do{if(ret!=0){if(be->recer != NULL){close_recer(be,ret);}return -1;}}while(0)
#define UGLY_TIMEOUT_FIX
#define DO_W_STUFF_IN_FIXED_BLOCKS

int sbuf_check(struct buffer_entity *be, int tout){
  int ret = 0;
  struct simplebuf * sbuf = (struct simplebuf * )be->opt;
  //while ((ret = be->recer->check(be->recer))>0){
  DD("Checking for ready writes. asyndiff: %d, diff: %d",,sbuf->asyncdiff,sbuf->diff);
  /* Still doesn't really wait DURR */
  ret = be->recer->check(be->recer, 0);
  if(ret > 0){
    /* Write done so decrement async_writes_submitted */
    //sbuf->async_writes_submitted--;
    D("%lu Writes complete on seqnum %lu",, ret/sbuf->opt->packet_size, sbuf->fh->id);
    unsigned long num_written;

    num_written = ret/sbuf->opt->packet_size;

    sbuf->asyncdiff-=num_written;
  }
  else if (ret == AIO_END_OF_FILE){
    //D("End of file on id %lu",, sbuf->file_seqnum);
    D("End of file on id %lu",, sbuf->fh->id);
    sbuf->asyncdiff = 0;
  }
  else if (ret == 0){
    DD("No writes to report on %lu",, sbuf->fh->id);
#ifdef UGLY_TIMEOUT_FIX
    if(tout == 1)
      usleep(1000);
#endif
  }
  else{
    E("Error in write check on seqdum %lu",, sbuf->fh->id);
    return -1;
  }
  return 0;
}
int sbuf_acquire(void* buffo, void *opti,void* acq){
  struct buffer_entity * be = (struct buffer_entity*)buffo;
  struct simplebuf * sbuf = (struct simplebuf *)be->opt;
  sbuf->opt = (struct opt_s *)opti;
  if(sbuf->opt->optbits & USE_RX_RING){
    struct rxring_request* rxr = (struct rxring_request*)acq;
    /* This threads responsible area */
    sbuf->buffer = sbuf->opt->buffer + ((long unsigned)rxr->bufnum)*(sbuf->opt->packet_size*sbuf->opt->buf_num_elems);
  }

  sbuf->bufoffset = sbuf->buffer;
  memcpy(sbuf->filename_old, sbuf->opt->filename, sizeof(char)*FILENAME_MAX);
  /* If we're reading and we've been acquired: we need to load the buffer */
  if(sbuf->opt->optbits & READMODE){
    //sbuf->ready_to_act = 1;

  /* This might be a bit inefficient, but they shouldn't 	*/
  /* be very far from the root 	*/

    /*
    struct fileholder* fh = sbuf->opt->fileholders;
    while(fh != NULL){
      if(fh->id == seq){
	sbuf->fh = fh;
	break;
      }
      fh = fh->next;
    }
    */
    sbuf->fh = (struct fileholder*)acq;
  }
  else{
    /* If we used to have a inmem buffer ready for live, set it to no in mem */
    //sbuf->fh->status &= ~FH_INMEM;

    sbuf->fh = sbuf->fh_def; 
    zero_fileholder(sbuf->fh);
    sbuf->fh->id = *((long unsigned*)acq);
    sbuf->fh->status = FH_BUSY;
  }
  //sbuf->file_seqnum = seq;

  //fprintf(stdout, "\n\nDABUF:%lu\n\n", sbuf->buffer);
  return 0;
}
int sbuf_release(void* buffo){
  struct buffer_entity * be = (struct buffer_entity*)buffo;
  struct simplebuf * sbuf = (struct simplebuf *)be->opt;
  if(sbuf->opt->optbits & USE_RX_RING)
    sbuf->buffer = NULL;

  //sbuf->opt_old = sbuf->opt;
  //sbuf->file_seqnum_old = sbuf->file_seqnum;
  

  //sbuf->opt = sbuf->opt_default;
  //sbuf->file_seqnum = -1;
  return 0;
}
void preheat_buffer(void* buf, struct opt_s* opt){
  memset(buf, 0, opt->packet_size*(opt->buf_num_elems));
}
/*
   int sbuf_seqnumcheck(void* buffo, int seq){
   if(((struct simplebuf*)((struct buffer_entity*)buffo)->opt)->file_seqnum == seq)
    return 1;
  else
    return 0;
}
*/
int sbuf_free(void* buffo){
  /*
  if(buffo != NULL){
    struct buffer_entity * be = (struct buffer_entity*)buffo;
    free(be);
  }
  */
  (void)buffo;
  return 0;
}
int sbuf_identify(void* ent, void* val1, void* val2,int iden_type){
  struct buffer_entity *be = (struct buffer_entity*)ent;
  struct simplebuf* sbuf= (struct simplebuf*)be->opt;
  if(iden_type == CHECK_BY_SEQ){
    if((struct opt_s*)val2 == sbuf->opt){
    //if(strcmp(((struct opt_s*)val2)->filename,sbuf->filename_old) == 0){
      if(sbuf->fh->id == *((unsigned long*)val1)){
	D("Match!");
	return 1;
      }
      else
      return 0;
    }
    else
      return 0;
  }
  /* Check if we previously had a needed file	*/
  else if (iden_type == CHECK_BY_OLDSEQ){
    if(strcmp(((struct opt_s*)val2)->filename,sbuf->filename_old) == 0){
    //if((struct opt_s*)val2 == sbuf->opt){
      if(sbuf->fh->id == *((unsigned long*)val1)){
	D("Match!");
	return 1;
      }
      else
	return 0;
    }
    else
      return 0;
  }
  return iden_from_opt(sbuf->opt, val1, val2, iden_type);
  //return (const char*)sbuf->opt->filename;
}
int sbuf_init(struct opt_s* opt, struct buffer_entity * be){
  //Moved buffer init to writer(Choosable by netreader-thread)
  int err;
  struct simplebuf * sbuf = (struct simplebuf*) malloc(sizeof(struct simplebuf));
  if(sbuf == NULL)
    return -1;
  be->opt = sbuf;
  sbuf->opt = opt;
  be->recer =NULL;
  sbuf->bufnum = sbuf->opt->bufculum++;
  sbuf->optbits = sbuf->opt->optbits;

  sbuf->filename_old = (char*)malloc(sizeof(char)*FILENAME_MAX);
  //sbuf->file_seqnum_old = -1;
  
  sbuf->fh_def = (struct fileholder*)malloc(sizeof(struct fileholder));
  sbuf->fh = sbuf->fh_def;

  sbuf->opt_default = opt;

  //be->membranch = opt->membranch;
  //be->diskbranch = opt->diskbranch;
  D("Adding simplebuf to membranch");
  struct listed_entity *le = (struct listed_entity*)malloc(sizeof(struct listed_entity));
  CHECK_ERR_NONNULL(le, "malloc listed_entity");
  le->entity = (void*)be;
  le->child = NULL;
  le->father = NULL;
  le->acquire = sbuf_acquire;
  le->check = NULL; //sbuf_seqnumcheck;
  le->release = sbuf_release;
  le->close = sbuf_free;
  le->identify = sbuf_identify;
  be->self = le;
  add_to_entlist(sbuf->opt->membranch, be->self);
  D("Ringbuf added to membranch");

  
  /* Main arg for bidirectionality of the functions 		*/
  //sbuf->read = opt->read;

  //sbuf->opt->optbits = opt->optbits;
  //sbuf->file_seqnum = -1;
  sbuf->async_writes_submitted = 0;
  sbuf->ready_to_act = 0;

  //sbuf->opt->buf_num_elems = opt->buf_num_elems;
  //sbuf->opt->packet_size = opt->packet_size;
  //sbuf->writer_head = 0;
  //sbuf->tail = sbuf->hdwriter_head = (sbuf->opt->buf_num_elems-1);
  sbuf->diff =0;
  sbuf->asyncdiff =0;
  sbuf->running = 0;
  //sbuf->opt->do_w_stuff_every = opt->do_w_stuff_every;

  be->headlock = (LOCKTYPE *)malloc(sizeof(LOCKTYPE));
  CHECK_ERR_NONNULL(be->headlock, "Headlock malloc");
  be->iosignal = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
  CHECK_ERR_NONNULL(be->iosignal, "iosignal malloc");
  err = LOCK_INIT(be->headlock);
  CHECK_ERR("headlock init");
  err = pthread_cond_init(be->iosignal, NULL);
  CHECK_ERR("iosignal init");

  if(!(sbuf->opt->optbits & USE_RX_RING)){
    unsigned long hog_memory = sbuf->opt->buf_num_elems*sbuf->opt->packet_size;
    D("Trying to hog %lu MB of memory",,hog_memory/MEG);
    /* TODO: Make a check for available number of hugepages */
#ifdef HAVE_HUGEPAGES
    if(sbuf->optbits & USE_HUGEPAGE){
      /* Init fd for hugetlbfs					*/
      /* HUGETLB not yet supported on mmap so using MAP_PRIVATE	*/
      /*

	 char hugefs[FILENAME_MAX];
	 find_hugetlbfs(hugefs, FILENAME_MAX);

	 sprintf(hugefs, "%s%s%ld", hugefs,"/",pthread_self());
#if(DEBUG_OUTPUT)
fprintf(stdout, "RINGBUF: Initializing hugetlbfs file as %s\n", hugefs);
#endif

common_open_file(&(sbuf->huge_fd), O_RDWR,hugefs,0);
      //sbuf->huge_fd = open(hugefs, O_RDWR|O_CREAT, 0755);
      */

      /* TODO: Check other flags aswell				*/
      /* TODO: Not sure if shared needed as threads share id 	*/
      //sbuf->buffer = mmap(NULL, (sbuf->opt->buf_num_elems)*(sbuf->opt->packet_size), PROT_READ|PROT_WRITE , MAP_SHARED|MAP_HUGETLB, sbuf->huge_fd,0);
      //assert(hog_memory%sysconf(_SC_PAGESIZE) == 0);
#ifdef MMAP_NOT_SHMGET
      sbuf->buffer = mmap(NULL, hog_memory, PROT_READ|PROT_WRITE , MAP_ANONYMOUS|MAP_PRIVATE|MAP_HUGETLB, -1,0);
      if(sbuf->buffer ==MAP_FAILED){
	perror("MMAP");
	E("Couldn't allocate hugepages");
	//remove(hugefs);
	err = -1;
      }
      else{
	err = 0;
	D("mmapped to hugepages");
      }
#else
      sbuf->shmid = shmget(sbuf->bufnum, hog_memory, IPC_CREAT|IPC_EXCL|SHM_HUGETLB|SHM_NORESERVE|SHM_W|SHM_R);
      if(sbuf->shmid <0){
	E("Shmget failed");
	perror("shmget");
	return -1;
      }
      sbuf->buffer = shmat(sbuf->shmid, NULL, 0);
      if((long)sbuf->buffer == (long)-1){
	E("shmat failed");
	perror("shmat");
	return -1;
      }
#endif
      
    }
    else
#endif /* HAVE_HUGEPAGES */
    {
      long maxmem = sysconf(_SC_AVPHYS_PAGES)*sysconf(_SC_PAGESIZE);
      if(hog_memory > (long unsigned)maxmem){
	LOG("Max allocatable memory %ld MB. still reserving %lu MB more\n", maxmem/MEG, hog_memory/MEG);
	//return -1;
      }
      D("Memaligning buffer with %i sized %lu n_elements",,sbuf->opt->buf_num_elems, sbuf->opt->packet_size);
      err = posix_memalign((void**)&(sbuf->buffer), sysconf(_SC_PAGESIZE), hog_memory);
      //sbuf->buffer = malloc(((unsigned long)sbuf->opt->buf_num_elems)*((unsigned long)sbuf->opt->packet_size));
      //madvise(sbuf->buffer,   ((unsigned long)sbuf->opt->buf_num_elems)*((unsigned long)sbuf->opt->packet_size),MADV_SEQUENTIAL|MADV_WILLNEED); 
      preheat_buffer(sbuf->buffer, opt);

    }
    if (err < 0 || sbuf->buffer == 0) {
      perror("make_write_buffer");
      return -1;
    }
    D("Memory allocated");
  }
  else
    D("Memmapped in main so not reserving here");
  

  return 0;
}
int sbuf_close(struct buffer_entity* be, void *stats){
  (void)stats;

  struct simplebuf * sbuf = (struct simplebuf *) be->opt;
  D("Closing simplebuf for id %d ",, sbuf->bufnum);
  LOCK_DESTROY(be->headlock);
  LOCK_FREE(be->headlock);
  free(be->iosignal);

  //int ret = 0;
  /*
     if(be->recer->close != NULL)
     be->recer->close(be->recer, stats);
     */
  if(!(sbuf->optbits & USE_RX_RING)){
#ifdef HAVE_HUGEPAGES
    if(sbuf->optbits & USE_HUGEPAGE){
      munmap(sbuf->buffer, sbuf->opt->packet_size*sbuf->opt->buf_num_elems);
      /*
	 close(sbuf->huge_fd);
	 char hugefs[FILENAME_MAX];
	 find_hugetlbfs(hugefs, FILENAME_MAX);

	 sprintf(hugefs, "%s%s%ld", hugefs,"/",pthread_self());
#if(DEBUG_OUTPUT)
fprintf(stdout, "Removing hugetlbfs file %s\n", hugefs);
#endif
remove(hugefs);
*/
    }
    else
#endif /* HAVE_HUGEPAGES */
      free(sbuf->buffer);
  }
  else
    D("Not freeing mem. Done in main");
  D("Freeing structs");
  free(sbuf->fh_def);
  free(sbuf->filename_old);
  free(sbuf);
  //free(be);
  //free(be->recer);
  D("Simplebuf closed");
  return 0;
}
void close_recer(struct buffer_entity *be, int errornum){
  struct simplebuf *sbuf = (struct simplebuf *)be->opt;
  be->recer->handle_error(be->recer, errornum);
  if(sbuf->opt->optbits & READMODE){
    remove_specific_from_fileholders(sbuf->opt, sbuf->fh->diskid);
    set_free(sbuf->opt->membranch, be->self);
    sbuf->ready_to_act = 0;
  }
  //sbuf->file_seqnum = -1;
  be->recer = NULL;
}
int simple_end_transaction(struct buffer_entity *be){
  struct simplebuf *sbuf = (struct simplebuf*)be->opt;
  //void *offset = sbuf->buffer + (sbuf->opt->buf_num_elems - sbuf->diff)*sbuf->opt->packet_size;
  unsigned long wrote_extra = 0;
  long ret = 0;

  unsigned long count = sbuf->diff*(sbuf->opt->packet_size);
  //void * start = sbuf->buffer + (*tail * sbuf->opt->packet_size);
  while(count % BLOCK_ALIGN != 0){
    count++;
    wrote_extra++;
  }
  D("Have to write %lu extra bytes",, wrote_extra);

  ret = be->recer->write(be->recer, sbuf->bufoffset, count);
  if(ret<0){
    fprintf(stderr, "RINGBUF: Error in Rec entity write: %ld. Left to write:%d\n", ret,sbuf->diff);
    close_recer(be,ret);
    return -1;
  }
  else if (ret == 0){
    //Don't increment
  }
  else{
    //if(sbuf->opt->optbits & ASYNC_WRITE)
    //sbuf->async_writes_submitted++;
    if((unsigned long)ret != count){
      fprintf(stderr, "RINGBUF_H: Write wrote %ld out of %lu\n", ret, count);
      /* TODO: Handle incrementing so we won't lose data */
    }
    //increment_amount(sbuf, &(sbuf->hdwriter_head), endi);
    sbuf->diff = 0;
    sbuf->bufoffset = sbuf->buffer;
  }
  return 0;

}
void* sbuf_getbuf(struct buffer_entity *be, int ** diff){
  struct simplebuf *sbuf = (struct simplebuf*)be->opt;
  *diff = &(sbuf->diff);
  return sbuf->buffer;
}
int* sbuf_getinc(struct buffer_entity *be){
  return &((struct simplebuf*)be->opt)->diff;
}
int simple_write_bytes(struct buffer_entity *be){
  struct simplebuf * sbuf = (struct simplebuf *)be->opt;
  long ret;
  //unsigned long limit = sbuf->opt->do_w_stuff_every*(sbuf->opt->packet_size);
  unsigned long limit = sbuf->opt->do_w_stuff_every;

  unsigned long count = sbuf->diff * sbuf->opt->packet_size;
  //void * offset = sbuf->buffer + (sbuf->opt->buf_num_elems - sbuf->diff)*(sbuf->opt->packet_size);
  //void * offset = sbuf->buffer + (sbuf->opt->buf_num_elems - sbuf->diff)*(sbuf->opt->packet_size);
  ASSERT(sbuf->bufoffset + count <= sbuf->buffer+sbuf->opt->packet_size*sbuf->opt->buf_num_elems);
  ASSERT(count != 0);

#ifdef DO_W_STUFF_IN_FIXED_BLOCKS
  if(count > limit)
    count = limit;
#endif
  if (limit != count){
    D("Only need to finish transaction");
    return simple_end_transaction(be);
  }
  ASSERT(count % 4096 == 0);

  DD("Starting write with count %lu",,count);
  ret = be->recer->write(be->recer, sbuf->bufoffset, count);
  if(ret<0){
    E("RINGBUF: Error in Rec entity write: %ld. Left to write: %d offset: %lu count: %lu\n",, ret,sbuf->diff, (unsigned long)sbuf->bufoffset, count);
    close_recer(be,ret);
    return -1;
  }
  else if (ret == 0){
    if(!(sbuf->opt->optbits & READMODE)){
      E("Failed write with 0");
      //close_recer(be,ret);
    }
    //Don't increment
  }
  else{
    if((unsigned long)ret != count){
      fprintf(stderr, "RINGBUF_H: Write wrote %ld out of %lu\n", ret, count);
    }
    else{
#ifdef DO_W_STUFF_IN_FIXED_BLOCKS
      sbuf->diff-=sbuf->opt->do_w_stuff_every/sbuf->opt->packet_size;
#else
      sbuf->diff = 0;
#endif
      sbuf->bufoffset += count;
      if(sbuf->bufoffset >= sbuf->buffer +(sbuf->opt->buf_num_elems*sbuf->opt->packet_size))
	sbuf->bufoffset = sbuf->buffer;
    }
  }
  return 0;
}
int sbuf_async_loop(struct buffer_entity *be){
  struct simplebuf * sbuf = (struct simplebuf *)be->opt;
  int err;
  sbuf->asyncdiff = sbuf->diff;
  while(sbuf->asyncdiff > 0){
    if(sbuf->diff > 0){
      err = simple_write_bytes(be);
      CHECK_ERR_QUIET("Bytes written");
      DD("Checking async");
      err = sbuf_check(be,0);
      CHECK_ERR_QUIET("Async bytes written");
    }
    else if (be->recer == NULL){
      E("Lost recer. restart!");
      return -1;
    }
    else{
      DD("Only checking. Not writing. asyncdiff still %d",,sbuf->asyncdiff);
      err = sbuf_check(be,1);
      CHECK_ERR_QUIET("Async check");
    }
  }
  return 0;
}
int sbuf_sync_loop(struct buffer_entity *be){
  struct simplebuf * sbuf = (struct simplebuf *)be->opt;
  int err;
  DD("Starting write loop for %d elements",, sbuf->diff);
  while(sbuf->diff > 0){
    err = simple_write_bytes(be);
    CHECK_ERR_QUIET("sync bytes written");
    DD("Writeloop done. diff:  %d",,sbuf->diff);
  }
  return 0;
}
struct fileholder* livereceive_new_fileholder(struct opt_s* opt, struct fileholder *orig_fh){
  D("We have a live other!");
  int no_need_to_sort = 0;
  //struct fileholder * temp = NULL;
  /* Elegance going down */
  //struct fileholder * fh_prev = NULL;
  pthread_spin_lock(opt->augmentlock);
  struct fileholder* fh = opt->liveother->fileholders;
  /* Special case with first file */
  if(fh == NULL){
    D("Liveothers fileholders not yet started");
    opt->liveother->fileholders = fh = (struct fileholder*)malloc(sizeof(struct fileholder));
  }
  else{
    while(fh->next != NULL){
      fh=fh->next;
    }
    /*
       if(fh->id == sbuf->fh->id-1){
       D("Found spot to set new fileholder");
       if (fh->next != NULL){
       D("Next is not null and has file %lu. Saving it to temp",, fh->next->id);
       temp = fh->next;
       }
       fh->next = (struct fileholder*)malloc(sizeof(struct fileholder));
       fh = fh->next;
       break;
       }
       fh_prev = fh;
       fh = fh->next;
       */
    fh->next = (struct fileholder*)malloc(sizeof(struct fileholder));
    if(fh->id == orig_fh->id+1){
      D("Don't need to sort");
      no_need_to_sort = 1;
    }
    fh = fh->next;
    /*
       if (fh == NULL){
       D("Didn't find spot. Setting it anyway");
       fh_prev->next = fh = (struct fileholder*)malloc(sizeof(struct fileholder));
       }
       */
  }
  //zero_fileholder(fh);
  memcpy(fh, orig_fh, sizeof(struct fileholder));

  /* sbuf->fh_default will keep old safe */
  //sbuf->fh = fh;
  fh->next = NULL;
  if(!no_need_to_sort)
    arrange_by_id(opt->liveother);
  D("Fileholder for liveother set");
  pthread_spin_unlock(opt->augmentlock);
  return fh;
}
int write_buffer(struct buffer_entity *be){
  struct simplebuf* sbuf = (struct simplebuf*)be->opt;
  int ret;

  if(be->recer == NULL){
    ret = 0;
    D("Getting rec entity for buffer");
    /* If we're reading, we need a specific recorder_entity */
    if(sbuf->opt->optbits & READMODE){
      //memset(sbuf->filename_old, 0, sizeof(char)*FILENAME_MAX);
      D("Getting rec entity id %d for file %lu",, sbuf->fh->diskid, sbuf->fh->id);
      be->recer = (struct recording_entity*)get_specific(sbuf->opt->diskbranch, sbuf->opt, sbuf->fh->id, sbuf->running, sbuf->fh->diskid, &ret);
      /* TODO: This is a real bummer. Handle! 	*/
      if(ret !=0){
	E("Specific writer fails on acquired.");
	E("Shutting it down and removing from list");
	/* Not thread safe atm 			*/
	//Let the one using it close it!
	if(be->recer !=NULL){
	  close_recer(be,ret);
	}
	return -1;
      }
    }
    else{
      be->recer = (struct recording_entity*)get_free(sbuf->opt->diskbranch, sbuf->opt,((void*)&(sbuf->fh->id)), &ret);
      if(ret !=0){
	E("Error in acquired for random entity");
	E("Shutting faulty writer down");
	/* TODO: In daemonmode, the remove specific needs to exist also! */
	close_recer(be,ret);
	/* The next round will get a new recer */
	return -1;
      }
      else{
	sbuf->fh->diskid = be->recer->getid(be->recer);
	sbuf->fh->status = FH_INMEM;
	//memcpy(sbuf->filename_old, sbuf->opt->filename, sizeof(char)*FILENAME_MAX);
	if(sbuf->opt->optbits & LIVE_RECEIVING){
	  //memset(sbuf->filename_old, 0, sizeof(char)*FILENAME_MAX);
	  //struct fileholder * fh;
	  if(sbuf->opt->liveother != NULL){
	    sbuf->fh = livereceive_new_fileholder(sbuf->opt, sbuf->fh);
	  }
	  else
	    E("Live receiving, but liveother NULL");
	}
      }
    }
    CHECK_AND_EXIT(be->recer);
    D("Got rec entity");
  }

  if(sbuf->opt->optbits & ASYNC_WRITE){
    ret = sbuf_async_loop(be);
  }
  else{
    ret = sbuf_sync_loop(be);
  }
  if(ret != 0){
    E("Faulty recer");
    if(be->recer != NULL)
      close_recer(be,ret);
    return -1;
  }

  /* If we've succesfully written everything: set everything free etc */
  if(sbuf->diff == 0){

    /* Might have closed recer already 	*/
    if(be->recer != NULL)
      set_free(sbuf->opt->diskbranch, be->recer->self);
    sbuf->ready_to_act = 0;
    be->recer = NULL;

    if(sbuf->opt->optbits & READMODE){
      D("Read cycle complete. Setting self to loaded with %lu",, sbuf->fh->id);
      set_loaded(sbuf->opt->membranch, be->self);
    }
    else{
      D("Write cycle complete. Setting self to free");
      set_free(sbuf->opt->membranch, be->self);
    }
  }
  return ret;
}
void *sbuf_simple_write_loop(void *buffo){
  D("Starting simple write loop");
  struct buffer_entity * be = (struct buffer_entity *)buffo;
  struct simplebuf * sbuf = (struct simplebuf *)be->opt;
  int ret=0;
  int savedif=0;
  sbuf->running = 1;
  D("Start running");
  while(sbuf->running){
    /* Checks if we've finished a write and we're holding a writer 	*/
    /* In this case we need to free the writer and ourselves		*/
    while(sbuf->ready_to_act == 0 && sbuf->running == 1){
      LOCK(be->headlock);
      D("Sleeping on ready");
      pthread_cond_wait(be->iosignal, be->headlock);
      D("Woke up");
      UNLOCK(be->headlock);
    }

    if(sbuf->diff > 0){
      D("Blocking writes. Left to write %d",,sbuf->diff);
      savedif = sbuf->diff;
      ret = -1;

      //while(ret!= 0 && sbuf->running == 1){
      while(ret!= 0){
	/* Write failed so set the diff back to old value and rewrite	*/
	ret = write_buffer(be);
	if(ret != 0){
	  D("Error in rec. Returning diff and bufoffset");
	  sbuf->diff = savedif;
	  sbuf->bufoffset = sbuf->buffer;
	  if(sbuf->opt->optbits & READMODE){
	    D("Error on drive with readmode on. File is skipped and set self to free");
	    //Common_write handles the fileholder-settings
	    sbuf->ready_to_act = 0;
	    set_free(sbuf->opt->membranch, be->self);
	    ret=0;
	  }
	}
	/*If everything went great */
	else{
	  if (sbuf->opt->optbits & LIVE_RECEIVING){
	    if(sbuf->opt->liveother != NULL){
	      D("Setting FH_ONDISK for file %lu,",, sbuf->fh->id);
	      pthread_spin_lock(sbuf->opt->liveother->augmentlock);
	      sbuf->fh->status |= FH_ONDISK;
	      pthread_spin_unlock(sbuf->opt->liveother->augmentlock);
	    }
	  }
	}
      }
    }
    }
    D("Finished on id %d",,sbuf->bufnum);
    pthread_exit(NULL);
  }
  void sbuf_cancel_writebuf(struct buffer_entity *be){
    D("Cancelling request for buffer");
    struct simplebuf *sbuf = be->opt;
    sbuf->ready_to_act = 0;
    set_free(sbuf->opt->membranch, be->self);
  }

  void sbuf_stop_running(struct buffer_entity *be){
    D("Stopping sbuf thread");
    ((struct simplebuf*)be->opt)->running = 0;
    LOCK(be->headlock);
    pthread_cond_signal(be->iosignal);
    UNLOCK(be->headlock);
    D("Stopped and signalled");
  }
  void sbuf_set_ready(struct buffer_entity *be){
    ((struct simplebuf*)be->opt)->ready_to_act = 1;
  }
  int sbuf_init_buf_entity(struct opt_s * opt, struct buffer_entity *be){
    be->init = sbuf_init;
    //be->write = sbuf_aio_write;
    //be->get_writebuf = sbuf_get_buf_to_write;
    be->simple_get_writebuf = sbuf_getbuf;
    be->get_inc = sbuf_getinc;
    //be->wait = sbuf_wait;
    be->close = sbuf_close;
    be->write_loop = sbuf_simple_write_loop;
    be->stop = sbuf_stop_running;
    be->set_ready = sbuf_set_ready;
    be->acquire = sbuf_acquire;
    be->cancel_writebuf = sbuf_cancel_writebuf;

    return be->init(opt,be); 
  }
