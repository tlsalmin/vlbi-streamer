#ifndef AIOWRITER
#define AIOWRITER
#include "streamer.h"
#include "aioringbuf.h"
//define IOVEC
//Stuff stolen from
//http://stackoverflow.com/questions/8629690/linux-async-io-with-libaio-performance-issue

//TODO: Change these to void * pointer type
//for interchangeable writer end per
//buffer or reusability of buffer
int aiow_init(void * ringbuf, void * recpoint);
int aiow_write(void * ringbuf, void * recpoint, int diff);
int aiow_check(void * recpoint, void * rbuf);
int aiow_close(void * ioinfo);
int aiow_wait_for_write(struct rec_point * recpoint);
int aiow_init_rec_entity(struct opt_s * opt, struct recording_entity * re);
#endif
