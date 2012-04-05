#ifndef AIOWRITER
#define AIOWRITER
#include "streamer.h"
//#include "ringbuf.h"
//define IOVEC
//Stuff stolen from
//http://stackoverflow.com/questions/8629690/linux-async-io-with-libaio-performance-issue

int aiow_init(struct opt_s *opt, struct recording_entity * re);
long aiow_write(struct recording_entity * re, void * start, size_t count);
long aiow_check(struct recording_entity * re);
int aiow_close(struct recording_entity * re, void * stats);
int aiow_wait_for_write(struct recording_entity * re);
int aiow_init_rec_entity(struct opt_s * opt, struct recording_entity * re);
//int aiow_write_index_data(struct recording_entity* re, void* data, int count);
int aiow_init_dummy(struct opt_s *opt, struct recording_entity *re);
const char * aiow_get_filename(struct recording_entity *re);
/*
int aiow_nofpacks(struct recording_entity *re);
int* aiow_pindex(struct recording_entity *re);
*/
#endif
