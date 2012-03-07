#include <stdio.h>
#include <malloc.h>
#include <sys/uio.h>
#include "aioringbuf.h"
#include "aiowriter.h"
#define HD_WRITE_N_SIZE 2048

void *rbuf_init(int elem_size, int num_elems){
  //int err;
  struct ringbuf * rbuf = (struct ringbuf*) malloc(sizeof(struct ringbuf));
  rbuf->num_elems = num_elems;
  rbuf->elem_size = elem_size;
  //Moved buffer init to writer(Choosable by netreader-thread)
  //TODO: Make custom writer to enable changing of writetech easily
  //rbuf->buffer = (void*) malloc(rbuf->num_elems*rbuf->elem_size);
  //err = posix_memalign((void**)&(rbuf->buffer), sysconf(__SC
  rbuf->writer_head = 0;
  //rbuf->tail = rbuf->hdwriter_head = rbuf->num_elems;
  rbuf->tail = rbuf->hdwriter_head = 0;
  rbuf->ready_to_write = 1;
  //TODO: move this here
  //rbuf->latest_write_num = 0;
  return rbuf;
}
void rbuf_close(void * rbuf, struct rec_point * rp){
  //struct ringbuf * rb = (struct ringbuf * )rbuf;
  aiow_close((struct io_info*)rp->iostruct, (struct ringbuf *)rbuf);
  //free(rb->buffer);
  free((struct ringbuf*)rbuf);
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
void * rbuf_get_buf_to_write(struct ringbuf *rbuf){
  void *spot;
  if(!increment(rbuf, &(rbuf->writer_head), &(rbuf->tail)))
    spot = NULL;
  else
    void * spot = rbuf->buffer + (rbuf->writer_head*rbuf->elem_size);
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
//TODO: Add a field to the rbuf for storing amount of writable blocks
int rbuf_aio_write(void *rbuffer, struct rec_point * rp, int force){
  int ret = 0;

  struct ringbuf * rbuf = (struct ringbuf * )rbuffer;
  //HD writing. Check if job finished. Might also use message passing
  //in the future
  if(rbuf.ready_to_write < 1){
    if ((ret = aiow_check(rp, (void*)rbuf))>0){
#ifdef DEBUG_OUTPUT
      fprintf(stdout, "UDP_STREAMER: %d Writes complete. Cleared write block\n", ret);
#endif
      rbuf.ready_to_write += ret;
    }
    else if (ret < 0)
      fprintf(stderr, "UDP_STREAMER: AIOW check returned error %d", ret);
  }
  //No space in buffer. TODO: Wait only waits a static time
  else if (recv == 0){
    err = aiow_wait_for_write((void*)spec_ops->rp);
#ifdef DEBUG_OUTPUT
    fprintf(stdout, "UDP_STREAMER: Woke up\n");
#endif
    if(err < 0){
      perror("Waking from aio-sleep");
      return err;
    }
  }
  else{
    err= rbuf_aio_write(&(spec_ops->rbuf), spec_ops->rp, DONT_FORCE_WRITE);
    if(err < 0){
      perror("UDP_STREAMER: Aiow write error");
      return err;
    }
#ifdef DEBUG_OUTPUT
    else if (err > 0)
      fprintf(stdout, "UDP_STREAMER: %d Write request submitted\n", err);
#endif
  }


  int diff = diff_max(rbuf->hdwriter_head, rbuf->writer_head, rbuf->num_elems);
  if(diff > HD_WRITE_N_SIZE || force == FORCE_WRITE){
    //rbuf->ready_to_write = 0;
    ret = aiow_write((void*) rbuf, rp, diff);
    increment_amount(rbuf, &(rbuf->hdwriter_head), diff);
  }
  return ret;
  //Return not used yet, but saved for error handling
}
//alias for completiong from asynchronous write
inline void dummy_return_from_write(struct ringbuf *rbuf){
  int written = diff_max(rbuf->tail, rbuf->hdwriter_head, rbuf->num_elems);
  increment_amount(rbuf, &(rbuf->tail), written);
  rbuf->ready_to_write = 1;
}
//Alias for modularizing buffers etc.
int rbuf_check_hdevents(void * rbuf, void * rp){
  return aiow_check((struct ringbuf * )rbuf, (struct rec_point * )rp);
}
int rbuf_wait(void * rp){
  return aiow_wait_for_write((struct rec_point * )rp);
}
int rbuf_init_rec_entity(struct streamer_entity *se){
  int ret = 0;
  se.init_buffer = rbuf_init;
  se.write = rbuf_aio_write;
  se.get_writebuf = rbuf_get_buf_to_write;
  se.wait = rbuf_wait;
  se.close = rbuf_close;
  se.opt = se.init_buffer(BUF_ELEM_SIZE, BUF_NUM_ELEMS);
  if (se.opt == NULL)
    ret = -1;
  return ret;
}
