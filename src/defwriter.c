/*
 * defwriter.c -- Default IO writer for vlbistreamer
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
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include "config.h"

#include "streamer.h"
#include "defwriter.h"
#include "common_wrt.h"
#ifdef MADVISE_INSTEAD_OF_O_DIRECT
#include <sys/mman.h>
#endif

extern FILE* logfile;

long def_write(struct recording_entity * re, void * start, size_t count){
  long ret = 0;
  long total_w = 0;
  struct common_io_info * ioi = (struct common_io_info*) re->opt;
#ifdef MADVISE_INSTEAD_OF_O_DIRECT
  off_t oldoffset = lseek(ioi->fd, 0, SEEK_CUR);
#endif
  if(ioi->fd == 0){
    E("FD not set! Not writing to stdout!");
    return -1;
  }

  /* Loop until we've gotten everything written */
  while(count >0){
    DD("Issuing write of %lu with start %lu to %s",, count,(long unsigned)start, ioi->curfilename);
    if(ioi->opt->optbits & READMODE)
      ret = read(ioi->fd, start, count);
    else
      ret = write(ioi->fd, start, count);
    if(ret <=0){
      if(ret == 0 && (ioi->opt->optbits & READMODE)){
#if(DEBUG_OUTPUT)
	D("DEFWRITER: End of file!");
#endif
	total_w += count;
	ioi->bytes_exchanged += count;
#if(DAEMON)
	//if (pthread_spin_lock((ioi->opt->augmentlock)) != 0)
	  //E("spinlock lock");
	ioi->opt->bytes_exchanged += count;
	//if (pthread_spin_unlock((ioi->opt->augmentlock)) != 0)
	  //E("Spinlock unlock");
#endif
	return count;
      }
      else{
	perror("DEFWRITER: Error on write/read");
	E("DEFWRITER: Error happened on %s with count: %lu fd: %d error: %ld\n",, ioi->curfilename,  count,ioi->fd, ret);
	return errno;
	//return -1;
      }
    }
    else{
      DD("Write done for %ld\n",, ret);
      if((unsigned long)ret < count)
	fprintf(stderr, "DEFWRITER: Write wrote only %ld out of %lu\n", ret, count);
      total_w += ret;
      count -= ret;
      ioi->bytes_exchanged += ret;
#if(DAEMON)
      //if (pthread_spin_lock((ioi->opt->augmentlock)) != 0)
	//E("Spinlock lock");
      ioi->opt->bytes_exchanged += ret;
      //if (pthread_spin_unlock((ioi->opt->augmentlock)) != 0)
	//E("Spinlock unlock");
#endif
    }
  }
#ifdef MADVISE_INSTEAD_OF_O_DIRECT
	if(ioi->opt->optbits & READMODE){
	  ret = posix_fadvise(ioi->fd, oldoffset, total_w, POSIX_FADV_NOREUSE|POSIX_FADV_DONTNEED);
	}
	else{
	  ret = sync_file_range(ioi->fd,oldoffset,total_w, SYNC_FILE_RANGE_WAIT_BEFORE|SYNC_FILE_RANGE_WRITE|SYNC_FILE_RANGE_WAIT_AFTER);  
	  ret = posix_madvise(start,count,POSIX_MADV_SEQUENTIAL|POSIX_MADV_WILLNEED);
	}
#endif
  return total_w;
}
int def_get_w_fflags(){
#ifdef MADVISE_INSTEAD_OF_O_DIRECT
  return O_WRONLY|O_NOATIME;
#else
  return O_WRONLY|O_DIRECT|O_NOATIME|O_SYNC;
#endif
  //return O_WRONLY|O_NOATIME;
}
int def_get_r_fflags(){
#ifdef MADVISE_INSTEAD_OF_O_DIRECT
  return O_WRONLY|O_NOATIME;
#else
  return O_RDONLY|O_DIRECT|O_NOATIME|O_SYNC;
#endif
}

int def_init_def(struct opt_s *opt, struct recording_entity *re){
  common_init_common_functions(opt,re);
  //re->init = common_w_init;
  //re->close = common_close;
  //re->write_index_data = common_write_index_data;

  re->write = def_write;

  //re->get_n_packets = common_nofpacks;
  //re->get_packet_index = common_pindex;

  //re->get_filename = common_wrt_get_filename;
  //re->getfd = common_getfd;

  re->get_r_flags = def_get_r_fflags;
  re->get_w_flags = def_get_w_fflags;

  return re->init(opt,re);
}
int rec_init_dummy(struct opt_s *op , struct recording_entity *re){
  (void)op;
  (void)re;
  /*
     re->init = null;
     re-write = dummy_write_wrapped;
     */
  return 0;
}

