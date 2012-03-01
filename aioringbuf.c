#include <stdio.h>
#include <malloc.h>
#include "aioringbuf.h"

void rbuf_init(struct ringbuf * rbuf, int elem_size, int num_elems){
  rbuf->num_elems = num_elems;
  rbuf->elem_size = elem_size;
  rbuf->buffer = (void*) malloc(rbuf->num_elems*rbuf->elem_size);
  rbuf->writer_head = 0;
  rbuf->tail = rbuf->hdwriter_head = rbuf->num_elems;
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
  /*
  if(*head == *restrainer)
    return 0;
    */
  if(diff_max(*head, *restrainer, rbuf->num_elems-1) == 1){
    //*head = *restrainer;
    return 0;
  }
  else{
    increment_amount(rbuf, head, 1);
    return 1;
  }
}
//alias for moving packet writer head
inline int get_a_packet(struct ringbuf * rbuf){
  return increment(rbuf, &(rbuf->writer_head), &(rbuf->tail));
}
inline void * get_buf_to_write(struct ringbuf *rbuf){
  return rbuf->buffer + (rbuf->writer_head*rbuf->elem_size);
}
//alias for scheduling packets for writing
inline void dummy_write(struct ringbuf *rbuf){
  int writable = diff_max(rbuf->hdwriter_head, rbuf->writer_head, rbuf->num_elems);
  increment_amount(rbuf, &(rbuf->hdwriter_head), writable);
  rbuf->ready_to_write = 0;
  //Dummy write completes right away
  dummy_return_from_write(rbuf);
}
//alias for completiong from asynchronous write
inline void dummy_return_from_write(struct ringbuf *rbuf){
  int written = diff_max(rbuf->tail, rbuf->hdwriter_head, rbuf->num_elems);
  increment_amount(rbuf, &(rbuf->tail), written);
  rbuf->ready_to_write = 1;
}

