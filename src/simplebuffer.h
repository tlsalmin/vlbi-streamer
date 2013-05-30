/*
 * simplebuffer.h -- Header file for simple buffer
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
#ifndef SIMPLEBUF_H
#define SIMPLEBUF_H
#include "streamer.h"
//#define MMAP_NOT_SHMGET
struct simplebuf{
  struct opt_s *opt;
  /* Migrated diffs to work as number of bytes */
  long diff;
  //int diff;
  /* Used in init and close. Otherwise opts optbits is used 	*/
  /* TODO: Needs some refactoring to make more sensible		*/
  int optbits;
  long asyncdiff;
  //int asyncdiff;
  int async_writes_submitted;
  int running;
#if(HAVE_HUGEPAGES)
  int huge_fd;
#ifndef MMAP_NOT_SHMGET
  int shmid;
#endif
#endif
  long unsigned fileid;
  struct file_index*  fi;
  //struct fileholder* fh;
  //struct fileholder* fh_def;
  //unsigned long file_seqnum;
  int ready_to_act;
  int bufnum;
  void * bufoffset;

  struct opt_s *opt_default;
  //struct opt_s *opt_old;
  //char* filename_old;
  //unsigned long file_seqnum_old;
};
int sbuf_init(struct opt_s *opt, struct buffer_entity *be);
int sbuf_close(struct buffer_entity *be, void * stats);
void* sbuf_getbuf(struct buffer_entity *be, long** diff);
int sbuf_init_buf_entity(struct opt_s *opt, struct buffer_entity *be);
#endif
