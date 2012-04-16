#ifndef SENDFILE_STREAMER
#define SENDFILE_STREAMER
//#define UDP_STREAM_THREADS 12
#include <net/if.h>
#include <poll.h>
#include <linux/if_packet.h>
#include "streamer.h"

void * sendfile_init(struct opt_s *opt, struct buffer_entity * be);
void * sendfile_sender(void * opt);
void * sendfile_writer(void *opt);
void get_sendfile_stats(void *opt, void *stats);
void sendfile_stop(struct streamer_entity *se);
int close_sendfile(void *opt,void *stats);
int phandler_sequence(void * opts, void * buffer);
void sendfile_init_writer(struct opt_s *opt, struct streamer_entity *se, struct buffer_entity *be);
void sendfile_init_sender(struct opt_s *opt, struct streamer_entity *se, struct buffer_entity *be);
#endif //UDP_STREAMER
