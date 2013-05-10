/*
 * udpstream.h -- Header file for udp packet receiver/sender
 *
 * Written by Tomi Salminen (tlsalmin@gmail.com)
 * Copyright 2012 Metsähovi Radio Observatory, Aalto University.
 * All rights reserved
 * This file is part of vlbi-streamer.
 *
 * vlbi-streamer is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * vlbi-streamer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with vlbi-streamer.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */
#ifndef UDP_STREAMER
#define UDP_STREAMER
#include <net/if.h>
#include <poll.h>
#include <linux/if_packet.h>
#include "config.h"
#include "streamer.h"
#include "datatypes.h"
#include "common_filehandling.h"
#include "confighelper.h"
#include "sockethandling.h"

#define WRONGSIZELIMITBEFOREEXIT 20

#define UDPS_EXIT do {spec_ops->opt->total_packets = st.n_packets_probed;D("UDP_STREAMER: Closing sender thread. Total sent %lu, Supposed to send: %lu",, st.packets_sent, spec_ops->opt->total_packets); if(se->be != NULL){set_free(spec_ops->opt->membranch, se->be->self);} spec_ops->opt->status = STATUS_STOPPED;if(spec_ops->fd != 0){if(close(spec_ops->fd) != 0){E("Error in closing fd");}}pthread_exit(NULL);}while(0)
#define UDPS_EXIT_ERROR do {spec_ops->opt->total_packets = st.n_packets_probed; D("UDP_STREAMER: Closing sender thread. Left to send %lu, total sent: %lu",, st.packets_sent, spec_ops->opt->total_packets); if(se->be != NULL){set_free(spec_ops->opt->membranch, se->be->self);} spec_ops->opt->status = STATUS_ERROR;if(spec_ops->fd != 0){if(close(spec_ops->fd) != 0){E("Error in closing fd");}}pthread_exit(NULL);pthread_exit(NULL);}while(0)

/*
 * TODO: Change the function names to udps_<name>
 */
int setup_udp_socket(struct opt_s *opt, struct streamer_entity *se);
void * udp_sender(void * opt);
void * udp_receiver(void *opt);
void get_udp_stats(void *opt, void *stats);
//int phandler_sequence(struct streamer_entity * se, void * buffer);


int udps_init_udp_receiver( struct opt_s *opt, struct streamer_entity *se);

int udps_init_udp_sender( struct opt_s *opt, struct streamer_entity *se);


int udps_wait_function(struct sender_tracking *st, struct opt_s* opt);
int jump_to_next_buf(struct streamer_entity* se, struct resq_info* resq);
void*  calc_bufpos_general(void* header, struct streamer_entity* se, struct resq_info *resq);
inline int udps_handle_received_packet(struct streamer_entity* se, struct resq_info * resq, int received);
int handle_buffer_switch(struct streamer_entity *se , struct resq_info *resq);
  void reset_udpopts_stats(struct udpopts *spec_ops);
#endif //UDP_STREAMER
