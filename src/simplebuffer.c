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
#include "simplebuffer.h"
#include "streamer.h"

int sbuf_acquire(void* buffo, unsigned long seq){
  ((struct simplebuf*)((struct buffer_entity *)buffo)->opt)->file_seqnum = seq;
  return 0;
}
int sbuf_release(void* buffo){
  return 0;
}
void preheat_buffer(void* buf, struct opt_s* opt){
  memset(buf, 0, opt->buf_elem_size*(opt->buf_num_elems));
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

  //be->membranch = opt->membranch;
  //be->diskbranch = opt->diskbranch;
  D("Adding simplebuf to membranch");
  struct listed_entity *le = (struct listed_entity*)malloc(sizeof(struct listed_entity));
  le->entity = (void*)be;
  le->child = NULL;
  le->father = NULL;
  le->acquire = sbuf_acquire;
  le->release = sbuf_release;
  be->self = le;
  add_to_entlist(sbuf->opt->membranch, be->self);
  D("Ringbuf added to membranch");

  
  /* Main arg for bidirectionality of the functions 		*/
  //sbuf->read = opt->read;

  //sbuf->opt->optbits = opt->optbits;
  sbuf->async_writes_submitted = 0;
  sbuf->ready_to_act = 0;

  //sbuf->opt->buf_num_elems = opt->buf_num_elems;
  //sbuf->opt->buf_elem_size = opt->buf_elem_size;
  //sbuf->writer_head = 0;
  //sbuf->tail = sbuf->hdwriter_head = (sbuf->opt->buf_num_elems-1);
  sbuf->diff =0;
  sbuf->asyncdiff =0;
  sbuf->running = 0;
  //sbuf->opt->do_w_stuff_every = opt->do_w_stuff_every;

  be->headlock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
  be->iosignal = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
  pthread_mutex_init(be->headlock, NULL);
  pthread_cond_init(be->iosignal, NULL);



  /* TODO: Make choosable or just get rid of async totally 	*/
  //sbuf->async = opt->async;

  if(!(sbuf->opt->optbits & USE_RX_RING)){

#ifdef HAVE_HUGEPAGES
    if(sbuf->opt->optbits & USE_HUGEPAGE){
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
      //sbuf->buffer = mmap(NULL, (sbuf->opt->buf_num_elems)*(sbuf->opt->buf_elem_size), PROT_READ|PROT_WRITE , MAP_SHARED|MAP_HUGETLB, sbuf->huge_fd,0);
      sbuf->buffer = mmap(NULL, ((unsigned long)sbuf->opt->buf_num_elems)*((unsigned long)sbuf->opt->buf_elem_size), PROT_READ|PROT_WRITE , MAP_ANONYMOUS|MAP_SHARED|MAP_HUGETLB, 0,0);
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
    }
    else
#endif /* HAVE_HUGEPAGES */
    {
      D("Memaligning buffer");
      err = posix_memalign((void**)&(sbuf->buffer), sysconf(_SC_PAGESIZE), ((unsigned long)sbuf->opt->buf_num_elems)*((unsigned long)sbuf->opt->buf_elem_size));
      //sbuf->buffer = malloc(((unsigned long)sbuf->opt->buf_num_elems)*((unsigned long)sbuf->opt->buf_elem_size));
      //madvise(sbuf->buffer,   ((unsigned long)sbuf->opt->buf_num_elems)*((unsigned long)sbuf->opt->buf_elem_size),MADV_SEQUENTIAL|MADV_WILLNEED); 
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
int  sbuf_close(struct buffer_entity* be, void *stats){

  D("Closing simplebuf");
  struct simplebuf * sbuf = (struct simplebuf *) be->opt;
  pthread_mutex_destroy(be->headlock);
  free(be->headlock);
  free(be->iosignal);

  //int ret = 0;
  /*
     if(be->recer->close != NULL)
     be->recer->close(be->recer, stats);
     */
  if(!(sbuf->opt->optbits)){
#ifdef HAVE_HUGEPAGES
    if(sbuf->opt->optbits & USE_HUGEPAGE){
      munmap(sbuf->buffer, sbuf->opt->buf_elem_size*sbuf->opt->buf_num_elems);
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
  free(sbuf);
  //free(be->recer);
  D("Simplebuf closed");
  return 0;
}
int simple_end_transaction(struct buffer_entity *be){
  struct simplebuf *sbuf = (struct simplebuf*)be->opt;
  void *offset = sbuf->buffer + (sbuf->opt->buf_num_elems - sbuf->diff)*sbuf->opt->buf_elem_size;
  unsigned long wrote_extra = 0;
  long ret = 0;

  unsigned long count = sbuf->diff*(sbuf->opt->buf_elem_size);
  //void * start = sbuf->buffer + (*tail * sbuf->opt->buf_elem_size);
  while(count % BLOCK_ALIGN != 0){
    count++;
    wrote_extra++;
  }
  D("Have to write %lu extra bytes",, wrote_extra);

  ret = be->recer->write(be->recer, offset, count);
  if(ret<0){
    fprintf(stderr, "RINGBUF: Error in Rec entity write: %ld\n", ret);
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
  }
  return 0;

}
void* sbuf_getbuf(struct buffer_entity *be, int ** diff){
  struct simplebuf *sbuf = (struct simplebuf*)be->opt;
  *diff = &(sbuf->diff);
  return sbuf->buffer;
}
void close_recer(struct buffer_entity *be){
  struct simplebuf *sbuf = (struct simplebuf *)be->opt;
  E("Writer broken");
  remove_from_branch(sbuf->opt->diskbranch, be->recer->self,0);
  be->recer->close(be->recer,NULL);
  be->recer = NULL;
}
int simple_write_bytes(struct buffer_entity *be){
  struct simplebuf * sbuf = (struct simplebuf *)be->opt;
  long ret;
  //unsigned long limit = sbuf->opt->do_w_stuff_every*(sbuf->opt->buf_elem_size);
  unsigned long limit = sbuf->opt->do_w_stuff_every;

  unsigned long count = sbuf->diff * sbuf->opt->buf_elem_size;
  void * offset = sbuf->buffer + (sbuf->opt->buf_num_elems - sbuf->diff)*(sbuf->opt->buf_elem_size);

  if(count > limit)
    count = limit;
  else if (limit > count){
    D("Only need to finish transaction");
    return simple_end_transaction(be);
  }

  D("Starting write from %lu with count %lu",,offset,count);
  ret = be->recer->write(be->recer, offset, count);
  if(ret<0){
    E("RINGBUF: Error in Rec entity write: %ld\n",, ret);
    return -ret;
  }
  else if (ret == 0){
    if(!(sbuf->opt->optbits & READMODE)){
      E("Failed write with 0");
      close_recer(be);
    }
    //Don't increment
  }
  else{
    if((unsigned long)ret != count){
      fprintf(stderr, "RINGBUF_H: Write wrote %ld out of %lu\n", ret, count);
    }
    else{
      sbuf->diff-=sbuf->opt->do_w_stuff_every/sbuf->opt->buf_elem_size;
    }
  }
  return 0;
}
int sbuf_async_loop(struct buffer_entity *be){
  struct simplebuf * sbuf = (struct simplebuf *)be->opt;
  int ret;
  sbuf->asyncdiff = sbuf->diff;
  while(sbuf->asyncdiff > 0){
    if(sbuf->diff > 0){
      ret = simple_write_bytes(be);
      if(ret!=0){
	close_recer(be);
	return -1;
      }
      D("Checking async");
      ret = sbuf_check(be,0);
      if(ret!=0){
	close_recer(be);
	return -1;
      }
    }
    else if (be->recer == NULL){
      E("Lost recer. restart!");
      return -1;
    }
    else{
      D("Only checking. Not writing. asyncdiff still %d",,sbuf->asyncdiff);
      ret = sbuf_check(be,1);
      if(ret!=0){
	close_recer(be);
	return -1;
      }
    }
  }
  return 0;
}
int sbuf_sync_loop(struct buffer_entity *be){
  struct simplebuf * sbuf = (struct simplebuf *)be->opt;
  int ret;
  D("Starting write loop for %d elements",, sbuf->diff);
  while(sbuf->diff > 0){
    ret = simple_write_bytes(be);
    if(ret!=0){
      close_recer(be);
      return -1;
    }
    D("Writeloop done. diff:  %d",,sbuf->diff);
  }
  return 0;
}
void *sbuf_simple_read_loop(void *buffo){
}
int write_buffer(struct buffer_entity *be){
  struct simplebuf* sbuf = (struct simplebuf*)be->opt;
  int ret;

  if(be->recer == NULL){
    D("Getting rec entity for buffer");
    be->recer = (struct recording_entity*)get_free(sbuf->opt->diskbranch, sbuf->file_seqnum);
    CHECK_AND_EXIT(be->recer);
    D("Got rec entity");
  }

  if(sbuf->opt->optbits & ASYNC_WRITE){
    ret = sbuf_async_loop(be);
  }
  else{
    ret =sbuf_sync_loop(be);
  }

  /* If we've succesfully written everything: set everything free etc */
  //if((sbuf->diff == 0 && be->recer != NULL )){
  if(sbuf->diff == 0){
    D("Write cycle complete. Setting self to free");
    set_free(sbuf->opt->membranch, be->self);
    /* Might have closed recer already */
    if(be->recer != NULL)
      set_free(sbuf->opt->diskbranch, be->recer->self);
    sbuf->ready_to_act = 0;
    be->recer = NULL;
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
  while(sbuf->running){
    /* Checks if we've finished a write and we're holding a writer 	*/
    /* In this case we need to free the writer and ourselves		*/
    while(sbuf->ready_to_act == 0 && sbuf->running == 1){
      D("Sleeping on ready");
      pthread_mutex_lock(be->headlock);
      pthread_cond_wait(be->iosignal, be->headlock);
      D("Woke up");
      pthread_mutex_unlock(be->headlock);
    }
    /*
       if(sbuf->running == 0)
       break;
       */

    if(sbuf->diff > 0){
      D("Blocking writes. Left to write %d",,sbuf->diff);
      savedif = sbuf->diff;
      ret = -1;

      //while(ret!= 0 && sbuf->running == 1){
      while(ret!= 0){
	/* Write failed so set the diff back to old value and rewrite	*/
	ret = write_buffer(be);
	if(ret != 0){
	  D("Error in rec. Returning diff");
	  sbuf->diff = savedif;
	}
      }
    }
    }
    /*
       D("Loop over. Finishing");
       if(sbuf->diff > 0){
       D("Blocking writes. Left to write %d",,sbuf->diff);
       savedif = sbuf->diff;
       ret = -1;

       while(ret!= 0){
// Write failed so set the diff back to old value and rewrite
ret = write_buffer(be);
if(ret != 0){
D("Error in rec. Returning diff");
sbuf->diff = savedif;
}
}
}
*/
D("Finished");
pthread_exit(NULL);
}
void sbuf_stop_running(struct buffer_entity *be){
  D("Stopping sbuf thread");
  ((struct simplebuf*)be->opt)->running = 0;
  pthread_mutex_lock(be->headlock);
  pthread_cond_signal(be->iosignal);
  pthread_mutex_unlock(be->headlock);
  D("Stopped and signalled");
}
void sbuf_set_ready(struct buffer_entity *be){
  ((struct simplebuf*)be->opt)->ready_to_act = 1;
}
int sbuf_check(struct buffer_entity *be, int tout){
  int ret = 0, returnable = 0;
  struct simplebuf * sbuf = (struct simplebuf * )be->opt;
  //while ((ret = be->recer->check(be->recer))>0){
  D("Checking for ready writes. asyndiff: %d, diff: %d",,sbuf->asyncdiff,sbuf->diff);
  /* Still doesn't really wait DURR */
  usleep(10000);
  ret = be->recer->check(be->recer, 0);
  if(ret > 0){
    /* Write done so decrement async_writes_submitted */
    //sbuf->async_writes_submitted--;
    D("%lu Writes complete.\n",, ret/sbuf->opt->buf_elem_size);
    unsigned long num_written;

    num_written = ret/sbuf->opt->buf_elem_size;

    sbuf->asyncdiff-=num_written;
  }
  else if (ret == 0){
    //NADA
  }
  else{
    E("Error in write check");
    return -1;
  }
  return 0;
}
int sbuf_init_buf_entity(struct opt_s * opt, struct buffer_entity *be){
  be->init = sbuf_init;
  //be->write = sbuf_aio_write;
  //be->get_writebuf = sbuf_get_buf_to_write;
  be->simple_get_writebuf = sbuf_getbuf;
  //be->wait = sbuf_wait;
  be->close = sbuf_close;
  if(!(opt->optbits & READMODE))
    be->write_loop = sbuf_simple_write_loop;
  else
    be->write_loop = sbuf_simple_read_loop;
  //be->simple_get_writebuf = simple_get_buf;
  be->stop = sbuf_stop_running;
  be->set_ready = sbuf_set_ready;
  //be->cancel_writebuf = sbuf_cancel_writebuf;

  return be->init(opt,be); 
}
