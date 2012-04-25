#include <stdio.h>
#include <malloc.h>
#include <sys/uio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include "config.h"
#ifdef HAVE_HUGEPAGES
#include <sys/mman.h>
#endif

#include "ringbuf.h"
#include "common_wrt.h"
#define DO_WRITES_IN_FIXED_BLOCKS

/* Check if we really have HUGEPAGE-support 	*/
/* moved to configure-script */
/*
#ifndef MAP_HUGETLB
#undef HAVE_HUGEPAGES
#endif
*/

int rbuf_init(struct opt_s* opt, struct buffer_entity * be){
  //Moved buffer init to writer(Choosable by netreader-thread)
  int err;
  struct ringbuf * rbuf = (struct ringbuf*) malloc(sizeof(struct ringbuf));
  if(rbuf == NULL)
    return -1;
  
  be->opt = rbuf;
  /* Main arg for bidirectionality of the functions 		*/
  //rbuf->read = opt->read;

  rbuf->optbits = opt->optbits;
  rbuf->async_writes_submitted = 0;

  rbuf->num_elems = opt->buf_num_elems;
  rbuf->elem_size = opt->buf_elem_size;
  //rbuf->writer_head = 0;
  //rbuf->tail = rbuf->hdwriter_head = (rbuf->num_elems-1);
  if(rbuf->optbits & READMODE){
    rbuf->hdwriter_head = rbuf->writer_head = 0;
    rbuf->tail = -1;
  /* If we're reading, we want to fill in the buffer 		*/
    //rbuf->tail = -1;
    //if(rbuf->optbits & ASYNC_WRITE)
      //rbuf->hdwriter_head =0;
  }
  else{
    rbuf->tail = rbuf->hdwriter_head = rbuf->writer_head = 0;
  }
  rbuf->running = 0;
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
  rbuf->is_blocked = 0;
#endif
  rbuf->do_w_stuff_every = opt->do_w_stuff_every;



  /* TODO: Make choosable or just get rid of async totally 	*/
  //rbuf->async = opt->async;

#ifdef HAVE_HUGEPAGES
  if(rbuf->optbits & USE_HUGEPAGE){
    /* Init fd for hugetlbfs					*/
    /* HUGETLB not yet supported on mmap so using MAP_PRIVATE	*/
    /*

    char hugefs[FILENAME_MAX];
    find_hugetlbfs(hugefs, FILENAME_MAX);
    
    sprintf(hugefs, "%s%s%ld", hugefs,"/",pthread_self());
#ifdef DEBUG_OUTPUT
    fprintf(stdout, "RINGBUF: Initializing hugetlbfs file as %s\n", hugefs);
#endif

    common_open_file(&(rbuf->huge_fd), O_RDWR,hugefs,0);
    //rbuf->huge_fd = open(hugefs, O_RDWR|O_CREAT, 0755);
    */

    /* TODO: Check other flags aswell				*/
    /* TODO: Not sure if shared needed as threads share id 	*/
    //rbuf->buffer = mmap(NULL, (rbuf->num_elems)*(rbuf->elem_size), PROT_READ|PROT_WRITE , MAP_SHARED|MAP_HUGETLB, rbuf->huge_fd,0);
    rbuf->buffer = mmap(NULL, ((unsigned long)rbuf->num_elems)*((unsigned long)rbuf->elem_size), PROT_READ|PROT_WRITE , MAP_ANONYMOUS|MAP_SHARED|MAP_HUGETLB, 0,0);
    if(rbuf->buffer ==MAP_FAILED){
      perror("MMAP");
      fprintf(stderr, "RINGBUF: Couldn't allocate hugepages\n");
      //remove(hugefs);
      err = -1;
    }
    else{
      err = 0;
#ifdef DEBUG_OUTPUT
      fprintf(stdout, "RINGBUF: mmapped to hugepages\n");
#endif
    }
  }
  else
#endif /* HAVE_HUGEPAGES */
  {
#ifdef DEBUG_OUTPUT
    fprintf(stdout, "RINGBUF: Memaligning buffer\n");
#endif
    err = posix_memalign((void**)&(rbuf->buffer), sysconf(_SC_PAGESIZE), ((unsigned long)rbuf->num_elems)*((unsigned long)rbuf->elem_size));
  }
  if (err < 0 || rbuf->buffer == 0) {
    perror("make_write_buffer");
    return -1;
  }
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "RINGBUF: Memory allocated\n");
#endif

  return 0;
}
int  rbuf_close(struct buffer_entity* be, void *stats){
//TODO: error handling
  struct ringbuf * rbuf = (struct ringbuf *)be->opt;
  //int ret = 0;
  if(be->recer->close != NULL)
    be->recer->close(be->recer, stats);
#ifdef HAVE_HUGEPAGES
  if(rbuf->optbits & USE_HUGEPAGE){
    munmap(rbuf->buffer, rbuf->elem_size*rbuf->num_elems);
    /*
    close(rbuf->huge_fd);
    char hugefs[FILENAME_MAX];
    find_hugetlbfs(hugefs, FILENAME_MAX);
    
    sprintf(hugefs, "%s%s%ld", hugefs,"/",pthread_self());
#ifdef DEBUG_OUTPUT
    fprintf(stdout, "Removing hugetlbfs file %s\n", hugefs);
#endif
    remove(hugefs);
    */
  }
  else
#endif /* HAVE_HUGEPAGES */
    free(rbuf->buffer);
  free(rbuf);
  //free(be->recer);
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "RINGBUF: Buffer closed\n");
#endif
  return 0;
}

//Calling this directly requires, that you know you won't go past a restrictor
void increment_amount(struct ringbuf * rbuf, int* head, int amount)
{
  int split = rbuf->num_elems - *head;
  //Gone over
  if(split <= amount){
    *head = (amount-split);
  }
  else{
    *head += amount;
  }
}
int diff_max(int a , int b, int max){
  if(a>b)
    return max-a+b;
  else
    return b-a;
}
/* Changing the packet-head (writed_head on record and tail on send) 	*/
/* are the only callers for this so increment diff			*/
int increment(struct ringbuf * rbuf, int *head, int *restrainer){
  //if(*head == (*restrainer-1)){
  if(diff_max(*head, *restrainer, rbuf->num_elems) == 1){
    return 0;
  }
  else{
    increment_amount(rbuf, head, 1);
    return 1;
  }
}
/* NOTE: Needs to be called inside critical section. Breaks modularity 	*/
/* a little, but else we could write a buffer that hasn't been filled 	*/
/* with data yet							*/
void * rbuf_get_buf_to_write(struct buffer_entity *be){
  struct ringbuf * rbuf = (struct ringbuf*) be->opt;
  void *spot;
  int *head, *rest;
    /*
     * In reading situation we try to fill the buffer from HD-values as fast as possible.
     * when asked for a buffer to send, we give the tail buffer and so the tail chases the head
     * where hdwriter_head tells how many spots we've gotten into the memory
     */
  if(!(rbuf->optbits & ASYNC_WRITE)){
    if(rbuf->optbits & READMODE){
      head = &(rbuf->tail);
      rest = &(rbuf->writer_head);
    }
    else{
      head = &(rbuf->writer_head);
      rest = &(rbuf->tail);
    }
  }
  else{
    if(rbuf->optbits & READMODE){
      head = &(rbuf->tail);
      rest = &(rbuf->hdwriter_head);
    }
    /*
     * In writing situation we simple fill the buffer with packets as fast as we can. Here
     * the head chases the tail
     */
    else{
      head = &(rbuf->writer_head);
      rest = &(rbuf->tail);
    }
  }

  if(!increment(rbuf, head, rest)){
    spot = NULL;
#ifdef DEBUG_OUTPUT
    fprintf(stdout, "RINGBUF_H: BUF FULL\n");
#endif
  }
  else
    spot = rbuf->buffer + (((long unsigned)*head)*(long unsigned)rbuf->elem_size);
  return spot;
}

/* The final function that really writes. tail is a pointer 	*/
/* since it isn't subject to critical section problems and	*/
/* It's the part we want to update				*/
int write_bytes(struct buffer_entity * be, int head, int *tail, int diff){
  struct ringbuf * rbuf = (struct ringbuf * )be->opt;
  /* diff should be precalculated  since it contains critical section stuff */
  int i;
  long ret;
  //int requests = 1+((rbuf->writer_head < rbuf->hdwriter_head) && rbuf->writer_head > 0);
  int requests = 1+((head < *tail) && head > 0);
  for(i=0;i<requests;i++){
    void * start;
    long count;
    long endi;
    if(i == 0){
      //start = rbuf->buffer + (rbuf->hdwriter_head * rbuf->elem_size);
      start = rbuf->buffer + (*tail * rbuf->elem_size);
      if(requests ==2){
	//endi = rbuf->num_elems - rbuf->hdwriter_head;
	endi = rbuf->num_elems - *tail;
	diff -= endi;
      }
      else
	endi = diff;
    }
    else{
      start = rbuf->buffer;
      endi = diff;
    }
    count = (endi) * ((long)(rbuf->elem_size));

#ifdef DEBUG_OUTPUT
      fprintf(stdout, "RINGBUF: Blocking writes. Write from %i to %lu diff %lu elems %i, %lu bytes\n", *tail, *tail+endi, endi, rbuf->num_elems, count);
#endif
    ret = be->recer->write(be->recer, start, count);
    if(ret<0){
      fprintf(stderr, "RINGBUF: Error in Rec entity write: %ld\n", ret);
      return -1;
    }
    else if (ret == 0){
      //Don't increment
    }
    else{
      if(rbuf->optbits & ASYNC_WRITE)
	rbuf->async_writes_submitted++;
      if(ret != count){
	fprintf(stderr, "RINGBUF_H: Write wrote %ld out of %lu\n", ret, count);
	/* TODO: Handle incrementing so we won't lose data */
      }
      //increment_amount(rbuf, &(rbuf->hdwriter_head), endi);
      pthread_mutex_lock(rbuf->headlock);
      increment_amount(rbuf, tail, endi);
      pthread_mutex_unlock(rbuf->headlock);
    }
  }
  return 0;
}
void rbuf_init_head_n_tail(struct ringbuf *rbuf, int** head, int** tail){
  if(!(rbuf->optbits & ASYNC_WRITE)){
    if(rbuf->optbits & READMODE){
      *tail = &(rbuf->writer_head);
      *head = &(rbuf->tail);
    }
    else{
      *tail = &(rbuf->tail);
      *head = &(rbuf->writer_head);
    }
  }
  else{
    //TODO: Something fishy here. Think this through
    if(rbuf->optbits & READMODE){
      *tail = &(rbuf->writer_head);
      *head= &(rbuf->tail);
    }
    else{
      *tail = &(rbuf->hdwriter_head);
      *head = &(rbuf->writer_head);
    }
  }
}
//rbuf->last_io_i = diff_final;
/* main func for reading and sleeping on full buffer */
void *rbuf_read_loop(void *buffo){
  struct buffer_entity * be = (struct buffer_entity *)buffo;
  struct ringbuf * rbuf = (struct ringbuf *)be->opt;
  int diff,ret=0;
  int *head, *tail;
  unsigned long packets_left = be->recer->get_n_packets(be->recer);
  unsigned long minp;
#ifdef DO_WRITES_IN_FIXED_BLOCKS
  int limited_head;
#endif
  rbuf_init_head_n_tail(rbuf,&head,&tail);

  /*
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "RINGBUF: Load the buffers\n");
#endif
  diff = rbuf->num_elems; //diff_max(*tail, *head, rbuf->num_elems);
  write_bytes(be, *head, tail, diff);
  packets_left -= diff;
  if(rbuf->optbits & ASYNC_WRITE){
    while(rbuf->async_writes_submitted > 0){
      rbuf_check(be);
      usleep(1000);
    }
  }
  pthread_mutex_lock(rbuf->headlock);
  pthread_cond_signal(rbuf->iosignal);
  pthread_mutex_unlock(rbuf->headlock);
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "RINGBUF: Buffers loaded\n");
#endif
  increment_amount(rbuf, tail, 1);
*/



  rbuf->running = 1;
  while(rbuf->running){
    minp = min(rbuf->do_w_stuff_every, packets_left);
#ifdef DEBUG_OUTPUT
    fprintf(stdout, "RINGBUF: Locking head\n");
#endif
    pthread_mutex_lock(rbuf->headlock);
    while(((diff = diff_max(*tail, *head, rbuf->num_elems)) < minp) && rbuf->running){
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
      rbuf->is_blocked = 1;
#endif
#ifdef DEBUG_OUTPUT
      fprintf(stdout, "RINGBUF: diff not enough %d. Going to sleep\n", diff);
#endif
      pthread_cond_wait(rbuf->iosignal, rbuf->headlock);
    }
#ifdef DEBUG_OUTPUT
	fprintf(stdout, "RINGBUF: Writing: diffmax reported: we have %d left. tail: %d, head: %d\n", diff, *tail, *head);
#endif
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
    rbuf->is_blocked = 0;
#endif

    pthread_mutex_unlock(rbuf->headlock);

#ifdef DO_WRITES_IN_FIXED_BLOCKS
    diff = minp;//diff < minp ? diff : minp;
    limited_head = (*tail+diff)%rbuf->num_elems;
#else
    limited_head = *head;
#endif
    if(diff > 0){
      if(minp < rbuf->do_w_stuff_every)
	ret = end_transaction(be,limited_head, tail,diff);
      else
	ret = write_bytes(be,limited_head, tail,diff);
      if(ret!=0){
	fprintf(stderr, "RINGBUG: Write returned error %d\n", ret);
	/* If streamer is blocked, wake it up 	*/
	/* TODO: Move to separate function 	*/
	be->se->stop(be->se);
	/*
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
	if(be->se->is_blocked(be->se))
#endif
*/
	{
	  pthread_mutex_lock(rbuf->headlock);
	  pthread_cond_signal(rbuf->iosignal);
	  pthread_mutex_unlock(rbuf->headlock);
	}
	rbuf->running = 0;
	break;
      }
      else{
	/* If we either just wrote the stuff in write_after_checks(synchronious 	*/
	/* blocking write or check in async mode) returned a > 0 amount of writes  */
	/* done */
	/* Update: Only signal if the streamer is blocked */
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
	if(!(rbuf->optbits & ASYNC_WRITE)&& be->se->is_blocked(be->se) == 1)
#else
	if(!(rbuf->optbits & ASYNC_WRITE))
#endif
	  {
	    // Signal if thread is waiting for space 
	    pthread_mutex_lock(rbuf->headlock);
#ifdef DEBUG_OUTPUT
	    fprintf(stdout, "RINGBUF: Trying to wake up streamer\n");
#endif
	    pthread_cond_signal(rbuf->iosignal);
	    pthread_mutex_unlock(rbuf->headlock);
	  }
	else
	  rbuf_check(be);
	/* Decrement the packet number we wan't to read */
#ifdef DEBUG_OUTPUT
	fprintf(stdout, "RINGBUF: Packets left %lu\n", packets_left);
#endif
	packets_left-=diff;
	/* If we've done loading all the packets, run ringbuf down */
	if(packets_left <= 0){
#ifdef DEBUG_OUTPUT
	  fprintf(stdout, "RINGBUF: All packets read!\n");
#endif
	  rbuf->running =0;
	}
      }
    }
  }
  // Main thread stopped so just write
  /* TODO: On asynch io, this will not check the last writes */
  if(ret >=0){
    /* TODO: Move away from silly async-wait times and 	*/
    /* Just use a counter for number of writes 		*/
    if(rbuf->optbits & ASYNC_WRITE){
#ifdef DEBUG_OUTPUT
      fprintf(stdout, "RINGBUF: Checking ASYNC writes status\n");
#endif
      while(rbuf->async_writes_submitted >0){
	usleep(100);
	rbuf_check(be);
      }
    }
  }
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "RINGBUF: Closing rbuf thread\n");
#endif
  pthread_exit(NULL);
}
/* Need to do 512 byte aligned write at end */
int end_transaction(struct buffer_entity * be, int head, int *tail, int diff){
  struct ringbuf * rbuf = (struct ringbuf * )be->opt;
  unsigned long wrote_extra = 0;
  long int ret = 0;
  
  unsigned long count = diff*(rbuf->elem_size);
  void * start = rbuf->buffer + (*tail * rbuf->elem_size);
  while(count % 512 != 0){
    count++;
    wrote_extra++;
  }
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "RINGBUF: Have to write %lu extra bytes\n", wrote_extra);
#endif

  ret = be->recer->write(be->recer, start, count);
  if(ret<0){
    fprintf(stderr, "RINGBUF: Error in Rec entity write: %ld\n", ret);
    return -1;
  }
  else if (ret == 0){
    //Don't increment
  }
  else{
    if(rbuf->optbits & ASYNC_WRITE)
      rbuf->async_writes_submitted++;
    if(ret != count){
      fprintf(stderr, "RINGBUF_H: Write wrote %ld out of %lu\n", ret, count);
      /* TODO: Handle incrementing so we won't lose data */
    }
    //increment_amount(rbuf, &(rbuf->hdwriter_head), endi);
    pthread_mutex_lock(rbuf->headlock);
    increment_amount(rbuf, tail, diff);
    pthread_mutex_unlock(rbuf->headlock);
  }
  return 0;
}
/* main func for writing and sleeping on buffer empty */ 
void *rbuf_write_loop(void *buffo){
  struct buffer_entity * be = (struct buffer_entity *)buffo;
  struct ringbuf * rbuf = (struct ringbuf *)be->opt;
  int diff,ret=0;
  int *head, *tail;
#ifdef DO_WRITES_IN_FIXED_BLOCKS
  int limited_head;
#endif
  rbuf_init_head_n_tail(rbuf,&head,&tail);
  rbuf->running = 1;
  while(rbuf->running){
    pthread_mutex_lock(rbuf->headlock);
    while(((diff = diff_max(*tail, *head, rbuf->num_elems)) < rbuf->do_w_stuff_every) && rbuf->running){
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
      rbuf->is_blocked = 1;
#endif
#ifdef DEBUG_OUTPUT
      fprintf(stdout, "RINGBUF: Not enough to write %d\n", diff);
#endif
      pthread_cond_wait(rbuf->iosignal, rbuf->headlock);
    }
#ifdef DEBUG_OUTPUT
    fprintf(stdout, "RINGBUF: Writing: diffmax reported: we have %d left. tail: %d, head: %d\n", diff, *tail, *head);
#endif
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
    rbuf->is_blocked = 0;
#endif
    pthread_mutex_unlock(rbuf->headlock);

    /* We might be stopped inbetween by the streamer entity */
    if(!rbuf->running)
      break;
#ifdef DO_WRITES_IN_FIXED_BLOCKS
    diff = diff < rbuf->do_w_stuff_every ? diff : rbuf->do_w_stuff_every;
    limited_head = (*tail+diff)%rbuf->num_elems;
#else
    limited_head = *head;
#endif
    if(diff > 0){
      ret = write_bytes(be,limited_head, tail,diff);
      if(ret!=0){
	fprintf(stderr, "RINGBUG: Write returned error %d. Stopping\n", ret);
	/* If streamer is blocked, wake it up 	*/
	/* TODO: Move to separate function 	*/
	be->se->stop(be->se);
	/*
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
if(be->se->is_blocked(be->se))
#endif
*/
	{
	  pthread_mutex_lock(rbuf->headlock);
	  pthread_cond_signal(rbuf->iosignal);
	  pthread_mutex_unlock(rbuf->headlock);
	}
	rbuf->running = 0;
	break;
      }
      /* We get a zero if its not a straight error, but not a full write 	*/
      /* Or an io_submit that wasn't queued					*/
      /*
	 else if (ret == 0){
	 fprintf(stderr, "RINGBUF: Incomplete or failed async write. Not stopping\n");
	 }
	 */
      else{
	/* If we either just wrote the stuff in write_after_checks(synchronious 	*/
	/* blocking write or check in async mode) returned a > 0 amount of writes  */
	/* done */
	/* Update: Only signal if the streamer is blocked */
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
	if(!(rbuf->optbits & ASYNC_WRITE) && be->se->is_blocked(be->se) == 1)
#else
	  if(!(rbuf->optbits & ASYNC_WRITE))
#endif
	  {
	    // Signal if thread is waiting for space 
	    pthread_mutex_lock(rbuf->headlock);
#ifdef DEBUG_OUTPUT
	    fprintf(stdout, "RINGBUF: Trying to wake up streamer\n");
#endif
	    pthread_cond_signal(rbuf->iosignal);
	    pthread_mutex_unlock(rbuf->headlock);
	  }
	  else
	    rbuf_check(be);
	/* Decrement the packet number we wan't to read */
      }
    }
  }
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "RINGBUF: Closing rbuf thread\n");
#endif
  // Main thread stopped so just write
  /* TODO: On asynch io, this will not check the last writes */
  if(ret >=0){
    /* TODO: Move away from silly async-wait times and 	*/
    /* Just use a counter for number of writes 		*/
    while((diff = diff_max(*tail, *head, rbuf->num_elems)) > 0){
      int ret = 0;
#ifdef DEBUG_OUTPUT
      fprintf(stdout, "RINGBUF: Closing up: diffmax reported: we have %d left in buffer after completion\n", diff);
#endif
#ifdef DO_WRITES_IN_FIXED_BLOCKS
      //aio can overload on write requests, so making just one big for it
      //if(!(rbuf->optbits & ASYNC_WRITE)){
      /* NOTE: Due to arbitrary size packets, we need to write extra data 	*/
      /* on the disk. IO_DIRECT requires block bytes aligned writes(512) 	*/
      /* So we increment it to the next full bytecount				*/
      if(diff < rbuf->do_w_stuff_every){
	ret = end_transaction(be, *head,tail,diff);
      }
      else{
	/*
	   while(diff*(rbuf->elem_size) % 512 != 0)
	   diff++;
	   }
	   */
	if (diff > rbuf->do_w_stuff_every)
	  diff = rbuf->do_w_stuff_every;
      //diff = diff < rbuf->do_w_stuff_every ? diff : rbuf->do_w_stuff_every;
      limited_head = (*tail+diff)%rbuf->num_elems;
      //}
      //else
      //limited_head = *head;
#else
      limited_head = *head;
#endif
      ret = write_bytes(be,limited_head,tail,diff);
      if(ret != 0){
	fprintf(stderr, "RINGBUF: Error in write. Exiting\n");
	break;
      }
      /* TODO: add a counter for n. of writes so we'll be sure we've written everything in async */
      if(rbuf->optbits & ASYNC_WRITE){
	usleep(100);
	rbuf_check(be);
      }
    }
  }
  if(rbuf->optbits & ASYNC_WRITE){
    while(rbuf->async_writes_submitted >0){
#ifdef DEBUG_OUTPUT
      fprintf(stdout, "RINGBUF: Sleeping and waiting for async to complete %d writes submitted\n", rbuf->async_writes_submitted);
#endif
      usleep(1000);
      rbuf_check(be);
    }
  }
}
pthread_exit(NULL);
}
int rbuf_check(struct buffer_entity *be){
  int ret = 0, returnable = 0;
  struct ringbuf * rbuf = (struct ringbuf * )be->opt;
  while ((ret = be->recer->check(be->recer))>0){
    /* Write done so decrement async_writes_submitted */
    if(rbuf->optbits & ASYNC_WRITE)
      rbuf->async_writes_submitted--;
#ifdef DEBUG_OUTPUT
    fprintf(stdout, "RINGBUF: %d Writes complete.\n", ret/rbuf->elem_size);
#endif
    returnable = ret;
    int * to_increment;
    int num_written;
    //TODO: Augment for bidirectionality
    if(rbuf->optbits & READMODE)
      to_increment = &(rbuf->hdwriter_head);
    else
      to_increment = &(rbuf->tail);

    /* ret tells us how many bytes were written */
    /* ret/buf_size = number of buffs we've written */

    num_written = ret/rbuf->elem_size;

    /* This might happen on last write, when we need to write some extra */
    /* stuff to keep the writes Block aligned				*/
    if(num_written > rbuf->do_w_stuff_every)
      num_written = rbuf->do_w_stuff_every;

    /* TODO: If we receive IO done out of order, we'll potentially release  */
    /* Buffer entries that haven't yet been released. Trusting that this 	*/
    /* won't happen 							*/

    pthread_mutex_lock(rbuf->headlock);
    increment_amount(rbuf, to_increment, num_written);
    pthread_cond_signal(rbuf->iosignal);
    pthread_mutex_unlock(rbuf->headlock);
    //rbuf->last_io_i = 0;
    //Only used cause IO_WAIT doesn't work with EXT4 yet 
    //ret = WRITE_COMPLETE_DONT_SLEEP;
  }
  return returnable;
}
/* Not used anymore! Use dummy loop */
int dummy_write(struct ringbuf *rbuf){
#ifdef DEBUG_OUTPUT
  //fprintf(stdout, "Using dummy\n");
#endif
  int writable = diff_max(rbuf->hdwriter_head, rbuf->writer_head, rbuf->num_elems);
  increment_amount(rbuf, &(rbuf->hdwriter_head), writable);
  //Dummy write completes right away
  dummy_return_from_write(rbuf);
  return 1;
}
//alias for completiong from asynchronous write
inline void dummy_return_from_write(struct ringbuf *rbuf){
  int written = diff_max(rbuf->tail, rbuf->hdwriter_head, rbuf->num_elems);
  increment_amount(rbuf, &(rbuf->tail), written);
}
int dummy_write_wrapped(struct buffer_entity *be, int force){
  struct ringbuf * rbuf = (struct ringbuf*)be->opt;
  dummy_write(rbuf);
  return 1;
}
void rbuf_stop_running(struct buffer_entity *be){
  ((struct ringbuf *)be->opt)->running = 0 ;
}
int rbuf_wait(struct buffer_entity * be){
  return be->recer->wait(be->recer);
}
long rbuf_fake_recer_write(struct recording_entity * re, void* s, size_t count){
  return count;
}
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
int rbuf_is_blocked(struct buffer_entity *be){
  return((struct ringbuf*)be->opt)->is_blocked;
}
#endif
int rbuf_init_dummy(struct opt_s * opt, struct buffer_entity *be){
  be->recer->write = rbuf_fake_recer_write;
  be->recer->close = NULL;
  be->recer->write_index_data = NULL;
  return rbuf_init_buf_entity(opt,be);
}
void rbuf_init_mutex_n_signal(struct buffer_entity *be, void * mutex, void * signal){
  struct ringbuf *rbuf = be->opt;
  rbuf->headlock = (pthread_mutex_t *)mutex;
  rbuf->iosignal = (pthread_cond_t *) signal;
  //return 1;
}
/* We only need to cancel it while writing, so use writer_head */
void rbuf_cancel_writebuf(struct buffer_entity *be){
  struct ringbuf *rbuf = be->opt;
  rbuf->writer_head = (rbuf->writer_head -1) % rbuf->num_elems;
}
int rbuf_init_buf_entity(struct opt_s * opt, struct buffer_entity *be){
  be->init = rbuf_init;
  //be->write = rbuf_aio_write;
  be->get_writebuf = rbuf_get_buf_to_write;
  be->wait = rbuf_wait;
  be->close = rbuf_close;
  if(opt->optbits & READMODE)
    be->write_loop = rbuf_read_loop;
  else
    be->write_loop = rbuf_write_loop;
  be->stop = rbuf_stop_running;
  be->cancel_writebuf = rbuf_cancel_writebuf;
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
  be->is_blocked = rbuf_is_blocked;
#endif
  be->init_mutex = rbuf_init_mutex_n_signal;

  return be->init(opt,be); 
}
