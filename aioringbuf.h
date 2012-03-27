#ifndef AIORINGBUF
#define AIORINGBUF
#include "streamer.h"
#define SLEEP_ON_IO

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
#ifdef SPLIT_RBUF_AND_IO_TO_THREAD
  int diff;
  int running;
#endif
  /* Neither used in real writing */
  int ready_to_io;
  int last_io_i;

  int async;
  int read;
#ifdef SPLIT_RBUF_AND_IO_TO_THREAD
  pthread_mutex_t *headlock;
  pthread_cond_t *iosignal;
  int is_blocked;
#endif
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
int rbuf_init_dummy(struct opt_s *opt, struct buffer_entity *be);

int rbuf_check(struct buffer_entity *be);
//int rbuf_write_index_data(struct buffer_entity* be, void * data, int size);
int write_bytes(struct buffer_entity * re, int head, int *tail, int diff);
#ifdef SPLIT_RBUF_AND_IO_TO_THREAD
void rbuf_init_mutex_n_signal(struct buffer_entity *be, void * mutex, void * signal);
#endif
#endif
