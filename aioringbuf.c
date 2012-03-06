#include <stdio.h>
#include <malloc.h>
#include <sys/uio.h>
#include "aioringbuf.h"
#include "aiowriter.h"
#define HD_WRITE_N_SIZE 16

void rbuf_init(struct ringbuf * rbuf, int elem_size, int num_elems){
  //int err;
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
}
void rbuf_close(struct ringbuf * rbuf){
  free(rbuf->buffer);
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
//alias for moving packet writer head
inline int get_a_packet(struct ringbuf * rbuf){
  if(rbuf->writer_head ==(rbuf->tail-1))
    return 0;
  else
    return increment(rbuf, &(rbuf->writer_head), &(rbuf->tail));
}
inline void * get_buf_to_write(struct ringbuf *rbuf){
  return rbuf->buffer + (rbuf->writer_head*rbuf->elem_size);
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
int rbuf_aio_write(struct ringbuf *rbuf, void * rp){
  int ret = 1;
  int diff = diff_max(rbuf->hdwriter_head, rbuf->writer_head, rbuf->num_elems);
  if(diff > HD_WRITE_N_SIZE){
    //rbuf->ready_to_write = 0;
    ret = aiow_write((void*) rbuf, rp, diff);
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

