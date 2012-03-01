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
  unsigned int pwriter_head;
  unsigned int hdwriter_head;
  unsigned int tail;
  void* buffer;
  unsigned int elem_size;
  unsigned int num_elems;
};

//Increments pwriter head. Returns negative, if 
//buffer is full
void rbuf_init(struct ringbuf * rbuf, unsigned int elem_size, unsigned int num_elems);
//Increment head, but not over restraint. If can't go further, return -1
int increment(struct ringbuf * rbuf, unsigned int * head, unsigned int* restraint, int amount);
#endif
