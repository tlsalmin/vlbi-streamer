#ifndef SENDFILE_STREAMER
#define SENDFILE_STREAMER
//#define UDP_STREAM_THREADS 12
#include <net/if.h>
#include <poll.h>
#include <linux/if_packet.h>
#include "streamer.h"

void * sendfile_sender(void * opt);
void * sendfile_writer(void *opt);
int close_sendfile(void *opt,void *stats);
int sendfile_init_writer(struct opt_s *opt, struct streamer_entity *se);
int sendfile_init_sender(struct opt_s *opt, struct streamer_entity *se);
#endif //UDP_STREAMER
