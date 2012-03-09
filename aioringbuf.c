#include <stdio.h>
#include <malloc.h>
#include <sys/uio.h>
#include <stdlib.h>
#include <unistd.h>
#include "aioringbuf.h"
//#include "aiowriter.h"
#define HD_WRITE_N_SIZE 2048

int rbuf_init(struct opt_s* opt, struct buffer_entity * be){
  //Moved buffer init to writer(Choosable by netreader-thread)
  //TODO: Make custom writer to enable changing of writetech easily
  int err;
  struct ringbuf * rbuf = (struct ringbuf*) malloc(sizeof(struct ringbuf));
  if(rbuf == NULL)
    return -1;
  
  be->opt = rbuf;

  rbuf->num_elems = opt->buf_num_elems;
  rbuf->elem_size = opt->buf_elem_size;
  rbuf->writer_head = 0;
  rbuf->tail = rbuf->hdwriter_head = 0;
  rbuf->ready_to_write = 1;
  rbuf->last_write_i = 0;

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
  //struct ringbuf * rb = (struct ringbuf * )rbuf;
  //aiow_close(be->rp->iostruct);
  //free(rb->buffer);
  be->recer->close(be->recer, stats);
  free(((struct ringbuf *)be->opt)->buffer);
  free((struct ringbuf*)be->opt);
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
int increment(struct ringbuf * rbuf, int *head, int *restrainer){
  //if(diff_max(*head, *restrainer, rbuf->num_elems-1) == 0){
  if(*head == (*restrainer-1)){
    return 0;
  }
  else{
    increment_amount(rbuf, head, 1);
    return 1;
  }
}
//Not used
struct iovec * gen_iov(struct ringbuf *rbuf, int * count, void *iovecs){

  //If we need to scatter
  if(rbuf->hdwriter_head > rbuf->writer_head && rbuf->writer_head > 1)
    *count = 2;
  else 
    *count = 1;
  
  struct iovec * iov = (struct iovec * )iovecs;

  //NOTE: Buffer diffs checked already when calling 
  //this func
  iov[0].iov_base = rbuf->buffer + (rbuf->elem_size*rbuf->writer_head);
  //If we haven't gone past the buffer end
  if(rbuf->writer_head > rbuf->writer_head){
    iov[0].iov_len = rbuf->elem_size * (rbuf->writer_head - rbuf->hdwriter_head);
  }
  else
  {
    iov[0].iov_len = rbuf->elem_size * (rbuf->num_elems - rbuf->hdwriter_head);
    if(rbuf->writer_head > 1){
      iov[1].iov_base = rbuf->buffer; 
      iov[1].iov_len = rbuf->elem_size * rbuf->writer_head;
    }
  }
  return iov;
}
/*
//alias for moving packet writer head
inline int get_a_packet(struct ringbuf * rbuf){
  if(rbuf->writer_head ==(rbuf->tail-1))
    return 0;
  else
    return increment(rbuf, &(rbuf->writer_head), &(rbuf->tail));
}
*/
void * rbuf_get_buf_to_write(struct buffer_entity *be){
  struct ringbuf * rbuf = (struct ringbuf*) be->opt;
  void *spot;
  if(!increment(rbuf, &(rbuf->writer_head), &(rbuf->tail)))
    spot = NULL;
  else
    spot = rbuf->buffer + (rbuf->writer_head*rbuf->elem_size);
  return spot;
}
//alias for scheduling packets for writing
int dummy_write(struct ringbuf *rbuf){
  fprintf(stdout, "USing dummy\n");
  int writable = diff_max(rbuf->hdwriter_head, rbuf->writer_head, rbuf->num_elems);
  increment_amount(rbuf, &(rbuf->hdwriter_head), writable);
  rbuf->ready_to_write = 0;
  //Dummy write completes right away
  dummy_return_from_write(rbuf);
  return 1;
}
inline int write_after_checks(struct ringbuf* rbuf, struct recording_entity *re, int force){
  int ret=0,i, diff_final;

  //TODO: Move this diff to a single int for faster processing
  diff_final = diff_max(rbuf->hdwriter_head, rbuf->writer_head, rbuf->num_elems);

  /*
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "Trying write for diff %d force is %d\n", diff_final, force);
#endif
*/
  if(diff_final > HD_WRITE_N_SIZE || force == FORCE_WRITE){
    int diff = diff_final;
    int requests = 1+((rbuf->writer_head < rbuf->hdwriter_head) && rbuf->writer_head > 0);
    rbuf->ready_to_write -= requests;
    for(i=0;i<requests;i++){
      void * start;
      size_t count;
      int endi;
      if(i == 0){
	start = rbuf->buffer + (rbuf->hdwriter_head * rbuf->elem_size);
	if(requests ==2){
	  endi = rbuf->num_elems - rbuf->hdwriter_head;
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
      fprintf(stdout, "RINGBUF: Blocking writes. Write from %i to %i diff %i elems %i\n", rbuf->hdwriter_head, rbuf->writer_head, endi, rbuf->num_elems);
#endif
      ret = re->write(re, start, count);
      if(ret<0)
	return ret;
      increment_amount(rbuf, &(rbuf->hdwriter_head), endi);
    }
    rbuf->last_write_i = diff_final;
  }
  return ret;
}
//TODO: Add a field to the rbuf for storing amount of writable blocks
int rbuf_aio_write(struct buffer_entity *be, int force){
  int ret = 0;

  struct ringbuf * rbuf = (struct ringbuf * )be->opt;

  //HD writing. Check if job finished. Might also use message passing
  //in the future
  if(rbuf->ready_to_write >= 1 || force){
    ret = write_after_checks(rbuf, be->recer, force);
  }
  else{
    if ((ret = be->recer->check(be->recer))>0){
#ifdef DEBUG_OUTPUT
      fprintf(stdout, "RINGBUF: %d Writes complete. Cleared write block to %d\n", ret, rbuf->ready_to_write+ret);
#endif
      rbuf->ready_to_write += ret;
      if(rbuf->last_write_i > 0){
	increment_amount(rbuf, &(rbuf->tail), rbuf->last_write_i);
	rbuf->last_write_i = 0;
      }

    }
    else if (ret < 0)
      fprintf(stderr, "RINGBUF: RINGBUF check returned error %d", ret);
  }
  //Wait handled in the receiver
  //Return not used yet, but saved for error handling
  return ret;
}
//alias for completiong from asynchronous write
inline void dummy_return_from_write(struct ringbuf *rbuf){
  int written = diff_max(rbuf->tail, rbuf->hdwriter_head, rbuf->num_elems);
  increment_amount(rbuf, &(rbuf->tail), written);
  rbuf->ready_to_write = 1;
}
/*
//Alias for modularizing buffers etc.
int rbuf_check_hdevents(struct buffer_entity * be){
  return be->recer->check(be->recer);
}
*/
int rbuf_wait(struct buffer_entity * be){
  return be->recer->wait(be->recer);
}
int rbuf_init_buf_entity(struct opt_s * opt, struct buffer_entity *be){
  be->init = rbuf_init;
  be->write = rbuf_aio_write;
  be->get_writebuf = rbuf_get_buf_to_write;
  be->wait = rbuf_wait;
  be->close = rbuf_close;
  
  return be->init(opt,be); 
}
