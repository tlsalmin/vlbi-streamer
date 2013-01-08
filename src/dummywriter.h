#ifndef DUMMYWRITER_H
#define DUMMYWRITER_H
#include "streamer.h"
long dummy_write(struct recording_entity * re, void * start, size_t count);
int dummy_init_dummy(struct opt_s *opt, struct recording_entity *re);
#endif
