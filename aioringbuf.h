#ifndef AIORINGBUF
#define AIORINGBUF
#define INC_PWRITER 0
#define INC_HDWRITER 1
#define INC_TAIL 2
#include "streamer.h"

struct ringbuf{
  /*
  void* pwriter_head;
  void* hdwriter_head;
  void* tail;
  */
  //This might just be easier with uints
  int writer_head;
  int hdwriter_head;
  int tail;
  void* buffer;
  int elem_size;
  int num_elems;
  int ready_to_write;
};

//Increments pwriter head. Returns negative, if 
//buffer is full
void *rbuf_init(int elem_size, int num_elems);
int rbuf_close(struct recording_entity re);
//Increment head, but not over restraint. If can't go further, return -1
void increment_amount(struct ringbuf * rbuf, int * head, int amount);
int diff_max(int a , int b, int max);
struct iovec * gen_iov(struct ringbuf *rbuf, int * count, void* iovecs);
//inline int get_a_packet(struct ringbuf *rbuf);
void * rbuf_get_buf_to_write(struct recording_entity *re);
//TODO: Change to configurable calls initialized in the ringbuf struct
int dummy_write(struct ringbuf *rbuf);
inline void dummy_return_from_write(struct ringbuf *rbuf);
int rbuf_aio_write(struct recording_entity* re, int force);
int rbuf_check_hdevents(void * rbuf, void * rp);
int rbuf_wait(struct recording_entity * re);
int rbuf_init_rec_entity(struct recording_entity *se);
#endif
