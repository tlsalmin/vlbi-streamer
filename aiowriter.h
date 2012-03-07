#ifndef AIOWRITER
#define AIOWRITER
#include "streamer.h"
#include "aioringbuf.h"
struct io_info{
  io_context_t ctx;
  struct rec_point * rp;
};
//define IOVEC
//Stuff stolen from
//http://stackoverflow.com/questions/8629690/linux-async-io-with-libaio-performance-issue

//TODO: Change these to void * pointer type
//for interchangeable writer end per
//buffer or reusability of buffer
int aiow_init(void * ringbuf, void * recpoint);
int aiow_write(void * ringbuf, void * recpoint, int diff);
int aiow_check(struct rec_point * recpoint, void * rbuf);
int aiow_close(struct io_info * ioinfo, struct ringbuf * ringbuf);
int aiow_wait_for_write(struct rec_point * recpoint);
#endif
