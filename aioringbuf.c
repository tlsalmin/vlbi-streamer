#include <stdio.h>
#include <malloc.h>
#include "aioringbuf.h"

void rbuf_init(struct ringbuf * rbuf, unsigned int elem_size, unsigned int num_elems){
  rbuf->num_elems = num_elems;
  rbuf->elem_size = elem_size;
  rbuf->buffer = (void*) malloc(rbuf->num_elems*rbuf->elem_size);
  rbuf->pwriter_head = rbuf->hdwriter_head = rbuf->tail = 0;
}

//TODO: A bit too complicated, but i'll try it
//If we hit the restraint, return 0. Else 1
int increment(struct ringbuf * rbuf, unsigned int* head, unsigned int *restraint, int amount)
{
  //Gone or going over
  if(*head > *restraint){
    int split = (rbuf->num_elems-1) - *head;
    if(split <= amount){
      *head = 0;
      amount--;
      //Ok split might be 0 
      return increment(rbuf, head, restraint, amount-split);
    }
    else{
      *head += amount;
      return 1;
    }
  }
  else{
    int split = *restraint - *head;
    //this shouldn't even happen
    if(split < amount){
      *head = *restraint;
      return 0;
    }
    else{
      *head += amount;
      return 1;
    }
  }
}
