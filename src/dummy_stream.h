#ifndef DUMMY_STREAM_H
#define DUMMY_STREAM_H
#include "streamer.h"

int setup_dummy_socket(struct opt_s *opt, struct streamer_entity *se);
void * dummy_sender(void * opt);
void * dummy_receiver(void *opt);
void get_dummy_stats(void *opt, void *stats);
void dummy_stop(struct streamer_entity *se);


int dummy_init_dummy_receiver( struct opt_s *opt, struct streamer_entity *se);

int dummy_init_dummy_sender( struct opt_s *opt, struct streamer_entity *se);
#endif // DUMMY_STREAM_H
