#ifndef DEFWRITER
#define DEFWRITER
#include "streamer.h"
//int def_init(struct opt_s *opt, struct recording_entity * re);
long def_write(struct recording_entity * re, void * start, size_t count);
//int def_close(struct recording_entity * re, void * stats);
int def_init_def(struct opt_s *opt, struct recording_entity *re);
#endif