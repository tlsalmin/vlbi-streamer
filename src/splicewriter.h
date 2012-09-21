/*
 * splicewriter.h -- Header file for splicewriter
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
#ifndef SPLICEWRITER
#define SPLICEWRITER
#include "streamer.h"
//int splice_init(struct opt_s *opt, struct recording_entity * re);
long splice_write(struct recording_entity * re, void * start, size_t count);
//int splice_close(struct recording_entity * re, void * stats);
int splice_init_splice(struct opt_s *opt, struct recording_entity *re);
#endif
