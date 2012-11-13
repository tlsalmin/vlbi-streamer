#ifndef DISK2FILE_H
#define DISK2FILE_H
#include "streamer.h"
void d2f_init_default_functions(struct opt_s *opt, struct streamer_entity *se);
int d2f_init( struct opt_s *opt, struct streamer_entity *se);

struct d2fopts_s{
  struct opt_s *opt;
  struct iover* offsets;
  unsigned long missing;
}
#endif
