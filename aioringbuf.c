#include <stdio.h>
#include <malloc.h>
#include "aioringbuf.h"

void rbuf_init(struct ringbuf * rbuf, int elem_size, int num_elems){
  rbuf->num_elems = num_elems;
  rbuf->elem_size = elem_size;
  rbuf->buffer = (void*) malloc(rbuf->num_elems*rbuf->elem_size);
  rbuf->pwriter_head = rbuf->hdwriter_head = rbuf->tail = 0;
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
  if(*head == *restrainer)
    return 0;
  else if(diff_max(*head, *restrainer, rbuf->num_elems-1) == 1){
    *head = *restrainer;
    return 0;
  }
  else{
    increment_amount(rbuf, head, 1);
    return 1;
  }
}
