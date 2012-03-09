#ifndef AIOWRITER
#define AIOWRITER
#include "streamer.h"
//#include "aioringbuf.h"
//define IOVEC
//Stuff stolen from
//http://stackoverflow.com/questions/8629690/linux-async-io-with-libaio-performance-issue

//TODO: Change these to void * pointer type
//for interchangeable writer end per
//buffer or reusability of buffer
int aiow_init(struct opt_s *opt, struct recording_entity * re);
int aiow_write(struct recording_entity * re, void * start, size_t count);
int aiow_check(struct recording_entity * re);
int aiow_close(struct recording_entity * re, void * stats);
int aiow_wait_for_write(struct recording_entity * re);
int aiow_init_rec_entity(struct opt_s * opt, struct recording_entity * re);
#endif
