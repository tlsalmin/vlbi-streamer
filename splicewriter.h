#ifndef SPLICEWRITER
#define SPLICEWRITER
#include "streamer.h"
//int splice_init(struct opt_s *opt, struct recording_entity * re);
int splice_write(struct recording_entity * re, void * start, size_t count);
//int splice_close(struct recording_entity * re, void * stats);
int splice_init_splice(struct opt_s *opt, struct recording_entity *re);
#endif
