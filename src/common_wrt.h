/*
 * common_wrt.h -- Header file for common IO functions for vlbi-streamer
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
#ifndef COMMON_IO_FUNCS
#define COMMON_IO_FUNCS
#include "streamer.h"
/* TODO: Remove duplicates */
struct common_io_info{
  int id;
  int status;
  //char * filename;
  char * curfilename;
  int fd;
  off_t offset;
  long long bytes_exchanged;
  off_t filesize;
  //INDEX_FILE_TYPE elem_size;
  int f_flags;
  INDEX_FILE_TYPE * indices;
  //int read;
  struct opt_s *opt;
  unsigned long file_seqnum;
  //unsigned int optbits;
  INDEX_FILE_TYPE indexfile_count;
  void * extra_param;
  int leftover_bytes;
  int shmid;
};
int common_open_file(int *fd, int flags, char * filename, loff_t fallosize);
int init_directory(struct recording_entity *re);
int common_handle_indices(struct common_io_info *ioi);
int common_w_init(struct opt_s* opt, struct recording_entity *re);
void get_io_stats(void * opt, void * st);
INDEX_FILE_TYPE * common_pindex(struct recording_entity *re);
unsigned long common_nofpacks(struct recording_entity *re);
int common_close(struct recording_entity * re, void * stats);
const char * common_wrt_get_filename(struct recording_entity *re);
int common_getfd(struct recording_entity *re);
void common_init_common_functions(struct opt_s *opt, struct recording_entity *re);
int handle_error(struct recording_entity *re, int errornum);
#if(HAVE_HUGEPAGES)
char * find_hugetlbfs(char *fsmount, int len);
#endif
#endif
