#ifndef AIORINGBUF
#define AIORINGBUF
#define INC_PWRITER 0
#define INC_HDWRITER 1
#define INC_TAIL 2

struct ringbuf{
  /*
  void* pwriter_head;
  void* hdwriter_head;
  void* tail;
  */
  //This might just be easier with uints
  int pwriter_head;
  int hdwriter_head;
  int tail;
  void* buffer;
  int elem_size;
  int num_elems;
};

//Increments pwriter head. Returns negative, if 
//buffer is full
void rbuf_init(struct ringbuf * rbuf, int elem_size, int num_elems);
//Increment head, but not over restraint. If can't go further, return -1
void increment_amount(struct ringbuf * rbuf, int * head, int amount);
#endif
