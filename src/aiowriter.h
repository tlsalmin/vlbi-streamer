/*
 * aiowriter.h -- Header file for asynchronius writer for vlbi-streamer
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
#ifndef AIOWRITER
#define AIOWRITER
#include "streamer.h"
//#include "ringbuf.h"
//define IOVEC
//Stuff stolen from
//http://stackoverflow.com/questions/8629690/linux-async-io-with-libaio-performance-issue

int aiow_init(struct opt_s *opt, struct recording_entity * re);
long aiow_write(struct recording_entity * re, void * start, size_t count);
long aiow_check(struct recording_entity * re, int tout);
int aiow_close(struct recording_entity * re, void * stats);
int aiow_wait_for_write(struct recording_entity * re);
int aiow_init_rec_entity(struct opt_s * opt, struct recording_entity * re);
//int aiow_write_index_data(struct recording_entity* re, void* data, int count);
const char * aiow_get_filename(struct recording_entity *re);
/*
int aiow_nofpacks(struct recording_entity *re);
int* aiow_pindex(struct recording_entity *re);
*/
#endif
