/*
 * sendfile_streamer.h -- Header file for socket splice implementation.
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
