/*
 * fanout.h -- Header file for fanout receiver
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
#ifndef FANOUT
#define FANOUT
#ifndef PACKET_FANOUT
#define PACKET_FANOUT		18
#define PACKET_FANOUT_HASH              0
#define PACKET_FANOUT_LB                1
#endif
#define THREADED
#ifndef THREADED
#define THREADS 1
#else
#define THREADS 5
#endif
#define CHECK_UP_TO_NEXT_RESERVED 0
#define CHECK_UP_ALL 1
#include <net/if.h>
#include <poll.h>
#include <linux/if_packet.h>
#include "streamer.h"

int fanout_setup_socket(struct opt_s *opt, struct streamer_entity *se);
void * fanout_thread(void *opt);
void fanout_get_stats(void *opt, void *stats);
int close_fanout(struct streamer_entity *se, void *stats);
int fanout_init_fanout(void * opt, struct streamer_entity *se);
#endif //FANOUT
