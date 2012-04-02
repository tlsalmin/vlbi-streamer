#include <stdio.h>
#include <malloc.h>
#include <sys/uio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "aioringbuf.h"
#include "common_wrt.h"
#define DO_WRITES_IN_FIXED_BLOCKS

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

  rbuf->num_elems = opt->buf_num_elems;
  rbuf->elem_size = opt->buf_elem_size;
  rbuf->writer_head = 0;
  rbuf->tail = rbuf->hdwriter_head = 0;
#ifdef SPLIT_RBUF_AND_IO_TO_THREAD
  if(rbuf->optbits & READMODE)
  /* If we're reading, we want to fill in the buffer 		*/
    rbuf->diff = rbuf->num_elems;
  else
    rbuf->diff = 0;
  rbuf->running = 1;
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
  rbuf->is_blocked = 0;
#endif
#endif
  rbuf->do_w_stuff_every = opt->do_w_stuff_every;



  /* TODO: Make choosable or just get rid of async totally 	*/
  //rbuf->async = opt->async;

#ifdef HUGEPAGESUPPORT
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
#endif /* HUGEPAGESUPPORT */
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
  int ret = 0;
  if(be->recer->close != NULL)
    be->recer->close(be->recer, stats);
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
    free(rbuf->buffer);
  free(rbuf);
  //free(be->recer);
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "RINGBUF: Buffer closed\n");
#endif
  return ret;
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
  if(*head == (*restrainer-1)){
    return 0;
  }
  else{
    increment_amount(rbuf, head, 1);
    //rbuf->diff++;
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
    fprintf(stdout, "AIORINGBUF: BUF FULL\n");
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
    if(ret<=0){
      fprintf(stderr, "Error in Rec entity write: %ld\n", ret);
      return -1;
    }
    else{
      if(ret != count){
	fprintf(stderr, "AIORINGBUF: Write wrote %ld out of %lu\n", ret, count);
	/* TODO: Handle incrementing so we won't lose data */
      }
      //increment_amount(rbuf, &(rbuf->hdwriter_head), endi);
      pthread_mutex_lock(rbuf->headlock);
      increment_amount(rbuf, tail, endi);
      pthread_mutex_unlock(rbuf->headlock);
    }
  }
  return 1;
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
#ifdef SPLIT_RBUF_AND_IO_TO_THREAD
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
  while(rbuf->running){
    pthread_mutex_lock(rbuf->headlock);
    while((diff = diff_max(*tail, *head, rbuf->num_elems)) < rbuf->do_w_stuff_every && rbuf->running){
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
      rbuf->is_blocked = 1;
#endif
      pthread_cond_wait(rbuf->iosignal, rbuf->headlock);
    }
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
    rbuf->is_blocked = 0;
#endif

    pthread_mutex_unlock(rbuf->headlock);

#ifdef DO_WRITES_IN_FIXED_BLOCKS
    diff = diff < rbuf->do_w_stuff_every ? diff : rbuf->do_w_stuff_every;
    limited_head = (*tail+diff)%rbuf->num_elems;
#else
    limited_head = *head;
#endif
    if(diff > 0){
      ret = write_bytes(be,limited_head, tail,diff);
      if(ret<=0){
	fprintf(stderr, "Write returned error %d\n", ret);
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
	if((!(rbuf->optbits & ASYNC_WRITE)  || rbuf_check(be) > 0 ) && be->se->is_blocked(be->se) == 1)
#else
	  if(!(rbuf->optbits & ASYNC_WRITE) || rbuf_check(be) > 0 )
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
      }
    }
  }
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "RINGBUF: Closing rbuf thread\n");
#endif
  // Main thread stopped so just write
  /* TODO: On asynch io, this will not check the last writes */
  while (ret >= 0 && (diff = diff_max(*tail, *head, rbuf->num_elems)) > 0){
#ifdef DEBUG_OUTPUT
    fprintf(stdout, "RINGBUF: diffmax reported: we have %d left in buffer after completion\n", diff);
#endif
    /* TODO: Enable when write_bytes working properly
#ifdef DO_WRITES_IN_FIXED_BLOCKS
    diff = diff < rbuf->do_w_stuff_every ? diff : rbuf->do_w_stuff_every;
#endif
*/
    ret = write_bytes(be,*head,tail,diff);
    /* TODO: add a counter for n. of writes so we'll be sure we've written everything in async */
    if(rbuf->optbits & ASYNC_WRITE){
      sleep(5);
      rbuf_check(be);
    }
  }
  pthread_exit(NULL);
}
#endif
int rbuf_check(struct buffer_entity *be){
  int ret = 0, returnable = 0;
  struct ringbuf * rbuf = (struct ringbuf * )be->opt;
  while ((ret = be->recer->check(be->recer))>0){
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

    /* TODO: If we receive IO done out of order, we'll potentially release  */
    /* Buffer entries that haven't yet been released. Trusting that this 	*/
    /* won't happen 							*/

#ifdef SPLIT_RBUF_AND_IO_TO_THREAD
    pthread_mutex_lock(rbuf->headlock);
#endif
    increment_amount(rbuf, to_increment, num_written);
#ifdef SPLIT_RBUF_AND_IO_TO_THREAD
    pthread_mutex_unlock(rbuf->headlock);
#endif
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
#ifdef SPLIT_RBUF_AND_IO_TO_THREAD
void rbuf_init_mutex_n_signal(struct buffer_entity *be, void * mutex, void * signal){
  struct ringbuf *rbuf = be->opt;
  rbuf->headlock = (pthread_mutex_t *)mutex;
  rbuf->iosignal = (pthread_cond_t *) signal;
  //return 1;
}
#endif
int rbuf_init_buf_entity(struct opt_s * opt, struct buffer_entity *be){
  be->init = rbuf_init;
  //be->write = rbuf_aio_write;
  be->get_writebuf = rbuf_get_buf_to_write;
  be->wait = rbuf_wait;
  be->close = rbuf_close;
  be->write_loop = rbuf_write_loop;
  be->stop = rbuf_stop_running;
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
  be->is_blocked = rbuf_is_blocked;
#endif
  be->init_mutex = rbuf_init_mutex_n_signal;

  return be->init(opt,be); 
}
