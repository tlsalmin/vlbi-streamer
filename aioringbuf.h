#ifndef AIORINGBUF
#define AIORINGBUF
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
  int last_write_i;
};

//Increments pwriter head. Returns negative, if 
//buffer is full
int rbuf_init(struct opt_s *opt, struct buffer_entity *be);
int rbuf_close(struct buffer_entity *be, void * stats);
//Increment head, but not over restraint. If can't go further, return -1
void increment_amount(struct ringbuf * rbuf, int * head, int amount);
int diff_max(int a , int b, int max);
struct iovec * gen_iov(struct ringbuf *rbuf, int * count, void* iovecs);
//inline int get_a_packet(struct ringbuf *rbuf);
void * rbuf_get_buf_to_write(struct buffer_entity *be);
//TODO: Change to configurable calls initialized in the ringbuf struct
int dummy_write(struct ringbuf *rbuf);
inline void dummy_return_from_write(struct ringbuf *rbuf);
int rbuf_aio_write(struct buffer_entity* be, int force);
int rbuf_check_hdevents(struct buffer_entity *be);
int rbuf_wait(struct buffer_entity * be);
int rbuf_init_buf_entity(struct opt_s *opt, struct buffer_entity *be);
#endif
