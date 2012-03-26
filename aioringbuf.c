#include <stdio.h>
#include <malloc.h>
#include <sys/uio.h>
#include <stdlib.h>
#include <unistd.h>
#include "aioringbuf.h"
#include <pthread.h>
//#include "aiowriter.h"
//Not used anymore
//#define HD_WRITE_N_SIZE 128

int rbuf_init(struct opt_s* opt, struct buffer_entity * be){
  //Moved buffer init to writer(Choosable by netreader-thread)
  int err;
  struct ringbuf * rbuf = (struct ringbuf*) malloc(sizeof(struct ringbuf));
  if(rbuf == NULL)
    return -1;
  
  be->opt = rbuf;
  /* Main arg for bidirectionality of the functions */
  rbuf->read = opt->read;

  rbuf->num_elems = opt->buf_num_elems;
  rbuf->elem_size = opt->buf_elem_size;
  rbuf->writer_head = 0;
  rbuf->tail = rbuf->hdwriter_head = 0;
#ifdef SPLIT_RBUF_AND_IO_TO_THREAD
  if(rbuf->read)
  /* If we're reading, we want to fill in the buffer */
    rbuf->diff = rbuf->num_elems;
  else
    rbuf->diff = 0;
  rbuf->running = 1;
#endif
  /* Neither used in real writing */
  rbuf->ready_to_io = 1;
  rbuf->last_io_i = 0;


  /* TODO: Make choosable or just get rid of async totally */
  rbuf->async = opt->async;

#ifdef DEBUG_OUTPUT
  fprintf(stdout, "RINGBUF: Memaligning buffer\n");
#endif
  err = posix_memalign((void**)&(rbuf->buffer), sysconf(_SC_PAGESIZE), rbuf->num_elems*rbuf->elem_size);
  if (err < 0 || rbuf->buffer == 0) {
    perror("make_write_buffer");
    return -1;
  }

  return 0;
}
int  rbuf_close(struct buffer_entity* be, void *stats){
//TODO: error handling
  int ret = 0;
  if(be->recer->close != NULL)
    be->recer->close(be->recer, stats);
  free(((struct ringbuf *)be->opt)->buffer);
  free((struct ringbuf*)be->opt);
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
  if(rbuf->async == 0){
    if(rbuf->read){
      head = &(rbuf->tail);
      rest = &(rbuf->writer_head);
    }
    else{
      head = &(rbuf->writer_head);
      rest = &(rbuf->tail);
    }
  }
  else{
    if(rbuf->read){
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
  }
  else
    spot = rbuf->buffer + ((*head)*rbuf->elem_size);
  return spot;
}
//alias for scheduling packets for writing
inline int write_after_checks(struct buffer_entity * be, int diff_final,int* head,int* tail){
  int ret=0; //,diff_final;
  /* Better for multithreading that we just copy the value  */
  //struct ringbuf * rbuf = (struct ringbuf * )be->opt;
  //int *head, *tail;
  /* Saved for incrementing */
  //int *tailp;

  /* In non async mode we won't actually even need the hdwriter_head */
  /*
  if(rbuf->async == 0)
    if(rbuf->read){
      head = &(rbuf->tail);
      tail = &(rbuf->writer_head);
      //tailp = &(rbuf->writer_head);
    }
    else{
      head = &(rbuf->writer_head);
      tail = &(rbuf->tail);
      //tailp = &(rbuf->hdwriter_head);
    }
  else{
    if(rbuf->read){
      head = &(rbuf->tail);
      tail = &(rbuf->writer_head);
      //tailp = &(rbuf->writer_head);
    }
    else{
      head = &(rbuf->writer_head);
      tail = &(rbuf->hdwriter_head);
    //tailp = &(rbuf->hdwriter_head);
    }
  }
  */

  //TODO: Move this diff to a single int for faster processing
  //Thought this doesn't take much processing compared to all of the interrupts
  //diff_final = diff_max(rbuf->hdwriter_head, rbuf->writer_head, rbuf->num_elems);
  
  /*
#ifdef SPLIT_RBUF_AND_IO_TO_THREAD
  pthread_mutex_lock(rbuf->headlock);
#endif
*/
  /* TODO: Make this separation work at the init level */
  //Special case when starting send
  /*
  if(rbuf->read && (*head == *tail))
    diff_final = rbuf->num_elems;
  else
    */
  //diff_final = diff_max(*tail, *head, rbuf->num_elems);
  /*
  diff_final = rbuf->diff;
#ifdef SPLIT_RBUF_AND_IO_TO_THREAD
  pthread_mutex_unlock(rbuf->headlock);
#endif
*/
  if(diff_final < 0)
    fprintf(stderr, "Omg write final smaller that 0\n");

  //if(diff_final > 0){
  ret = write_bytes(be,*head,tail,diff_final);
  //}
#ifdef SPLIT_RBUF_AND_IO_TO_THREAD
  /*
  if(ret>0){
    if(rbuf->async == 1){
      pthread_mutex_lock(rbuf->headlock);
      rbuf->diff -= diff_final;
      pthread_mutex_unlock(rbuf->headlock);
    }
  }
  else
  */
  if(ret<0)
    fprintf(stdout, "Error in write_bytes: %d\n", ret);
#endif
  return ret;
}

/* The final function that really writes. tail is a pointer 	*/
/* since it isn't subject to critical section problems and	*/
/* It's the part we want to update				*/
int write_bytes(struct buffer_entity * be, int head, int *tail, int diff){
  struct ringbuf * rbuf = (struct ringbuf * )be->opt;
  /* diff should be precalculated  since it contains critical section stuff */
  int i,ret;
  //int requests = 1+((rbuf->writer_head < rbuf->hdwriter_head) && rbuf->writer_head > 0);
  int requests = 1+((head < *tail) && head > 0);
  //rbuf->ready_to_io -= requests;
  for(i=0;i<requests;i++){
    void * start;
    size_t count;
    int endi;
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
    count = (endi) * (rbuf->elem_size);

#ifdef DEBUG_OUTPUT
      fprintf(stdout, "RINGBUF: Blocking writes. Write from %i to %i diff %i elems %i, %lu bytes\n", *tail, head, endi, rbuf->num_elems, count);
#endif
    ret = be->recer->write(be->recer, start, count);
    if(ret<=0){

      fprintf(stdout, "Error in Rec entity write: %d\n", ret);
      return ret;
    }
    //increment_amount(rbuf, &(rbuf->hdwriter_head), endi);
    pthread_mutex_lock(rbuf->headlock);
    increment_amount(rbuf, tail, endi);
    pthread_mutex_unlock(rbuf->headlock);
  }
  return ret;
}
//rbuf->last_io_i = diff_final;
#ifdef SPLIT_RBUF_AND_IO_TO_THREAD
/* main func for writing and sleeping on buffer empty */ 
void *rbuf_write_loop(void *buffo){
  struct buffer_entity * be = (struct buffer_entity *)buffo;
  struct ringbuf * rbuf = (struct ringbuf *)be->opt;
  int diff,ret;
  int *head, *tail;
  if(rbuf->async == 0){
    if(rbuf->read){
      tail = &(rbuf->writer_head);
      head = &(rbuf->tail);
    }
    else{
      tail = &(rbuf->tail);
      head = &(rbuf->writer_head);
    }
  }
  else{
    if(rbuf->read){
      tail = &(rbuf->writer_head);
      head= &(rbuf->tail);
    }
    else{
      tail = &(rbuf->hdwriter_head);
      head = &(rbuf->writer_head);
    }
  }
  while(rbuf->running){
    pthread_mutex_lock(rbuf->headlock);
    while((diff = diff_max(*tail, *head, rbuf->num_elems)) < DO_W_STUFF_EVERY && rbuf->running)
      pthread_cond_wait(rbuf->iosignal, rbuf->headlock);
    pthread_mutex_unlock(rbuf->headlock);
    ret = write_after_checks(be,diff, head, tail);
    if(ret<=0){
      fprintf(stderr, "Write returned error %d\n", ret);
      be->se->stop(be->se);
      rbuf->running = 0;
      break;
    }
    else{
      /* If we either just wrote the stuff in write_after_checks(synchronious 	*/
      /* blocking write or check in async mode) returned a > 0 amount of writes  */
      /* done */
      if(!rbuf->async || rbuf_check(be) > 0){
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
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "RINGBUF: Closing rbuf thread\n");
#endif
  // Main thread stopped so just write
  /* TODO: On asynch io, this will not check the last writes */
  //if(rbuf->diff > 0){
  if ((diff = diff_max(*tail, *head, rbuf->num_elems)) > 0){
    write_after_checks(be,diff,head,tail);
    if(rbuf->async){
      sleep(1);
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
      if(rbuf->read)
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
/*
//TODO: Add a field to the rbuf for storing amount of writable blocks
int rbuf_aio_write(struct buffer_entity *be, int force){
  int ret = 0;

  //struct ringbuf * rbuf = (struct ringbuf * )be->opt;

  //HD writing. Check if job finished. Might also use message passing
  //in the future
  if(!force){
    ret = rbuf_check(be);
  }
  //Kind of a double check, but the previous if affects these conditions
  //if(rbuf->ready_to_io >= 1 || force){
  ret = write_after_checks(be, force);
  //}
  return ret;
}
*/
int dummy_write(struct ringbuf *rbuf){
#ifdef DEBUG_OUTPUT
  //fprintf(stdout, "Using dummy\n");
#endif
  int writable = diff_max(rbuf->hdwriter_head, rbuf->writer_head, rbuf->num_elems);
  increment_amount(rbuf, &(rbuf->hdwriter_head), writable);
  rbuf->ready_to_io = 0;
  //Dummy write completes right away
  dummy_return_from_write(rbuf);
  return 1;
}
//alias for completiong from asynchronous write
inline void dummy_return_from_write(struct ringbuf *rbuf){
  int written = diff_max(rbuf->tail, rbuf->hdwriter_head, rbuf->num_elems);
  increment_amount(rbuf, &(rbuf->tail), written);
  rbuf->ready_to_io = 1;
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
/*
int rbuf_write_index_data(struct buffer_entity *be, void * data, int count){
  return be->recer->write_index_data(be->recer,data,count);
}
*/
int rbuf_init_dummy(struct opt_s * opt, struct buffer_entity *be){
  be->init = rbuf_init;
  be->write = dummy_write_wrapped;
  be->get_writebuf = rbuf_get_buf_to_write;
  be->wait = NULL;
  be->close = rbuf_close;
  //be->write_index_data = NULL;
  return be->init(opt,be);
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
  //be->write_index_data = common_write_index_data;
  be->init_mutex = rbuf_init_mutex_n_signal;

  return be->init(opt,be); 
}
