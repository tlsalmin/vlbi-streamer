/*
 * splicewriter.c -- Splice writer for vlbi-streamer
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
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h> /* For madvise */

#include "config.h"

#include "streamer.h"
#include "common_wrt.h"
#include "splicewriter.h"

extern FILE* logfile;

#define SP_LOCKIT do{ if(pthread_mutex_lock(&(sp->pmutex)) != 0){ sp->pstatus = PSTATUS_ERROR; return NULL; }}while(0)

#define SP_UNLOCKIT do {if(pthread_mutex_unlock(&sp->pmutex) != 0){ sp->pstatus = PSTATUS_ERROR; return NULL; }}while(0)

#define SP_WAIT do{ if(pthread_cond_wait(&sp->pcond, &sp->pmutex) != 0){ sp->pstatus = PSTATUS_ERROR; return NULL; }}while(0)

//#define PIPE_STUFF_IN_WRITELOOP
#define IOVEC_SPLIT_TO_IOV_MAX
/* Default max in 3.2.12. Larger possible if CAP_SYS_RESOURCE */
#define MAX_PIPE_SIZE 1048576
/* Read a claim that proper scatter gather requires fopen not open */
//#define USE_FOPEN
//#define MAX_IOVEC 16

struct splice_ops{
  struct iovec* iov;
  int pipes[2];
  int max_pipe_length;
  int pagesize;
  off_t tosplice;
  pthread_t partner;
  pthread_mutex_t pmutex;
  pthread_cond_t pcond;
  int pstatus;
#ifdef USE_FOPEN
  FILE* fd;
#endif
};
void * partnerloop(void* opts)
{
  struct common_io_info *ioi = (struct common_io_info*)opts;
  struct splice_ops * sp = (struct splice_ops*)ioi->extra_param;
  long temp_tosplice;
  int err;
  SP_LOCKIT;
  while(sp->pstatus > 0)
  {
    while(sp->pstatus == 1)
      SP_WAIT;
    temp_tosplice = sp->tosplice;
    SP_UNLOCKIT;

    while(temp_tosplice > 0)
    {
      if(ioi->opt->optbits & READMODE)
      {
#ifdef USE_FOPEN
	err = splice(fileno(sp->fd), NULL, sp->pipes[1], NULL, temp_tosplice, SPLICE_F_MOVE|SPLICE_F_MORE);
#else
	err = splice(ioi->fd, NULL, sp->pipes[1], NULL, temp_tosplice, SPLICE_F_MOVE|SPLICE_F_MORE);
#endif
      }
      else
      {
      }
      if(err < 0)
      {
	E("Error in splice partner");
	SP_LOCKIT;
	sp->pstatus = -1;
	SP_UNLOCKIT;
	return NULL;
      }
      temp_tosplice-=err;
    }

    SP_LOCKIT;
    if(sp->pstatus >0)
      sp->pstatus = 1;
  }
  sp->pstatus =0;
  SP_UNLOCKIT;
  return NULL;
}
int init_splice(struct opt_s *opts, struct recording_entity * re){
  int err;
  int ret = common_w_init(opts, re);
  int maxbytes_inpipe;
  if(ret < 0)
    return -1;
  struct common_io_info * ioi = (struct common_io_info*)re->opt;
  struct splice_ops * sp = (struct splice_ops*)malloc(sizeof(struct splice_ops));
  CHECK_ERR_NONNULL(sp, "Splice ops malloc");

#ifndef PIPE_STUFF_IN_WRITELOOP
  ret = pipe(sp->pipes);
  if(ret<0)
    return -1;
#endif
  /* TODO: Handle error for pipe size change */
#ifdef F_SETPIPE_SZ
  fcntl(sp->pipes[1], F_SETPIPE_SZ, MAX_PIPE_SIZE);
  maxbytes_inpipe = fcntl(sp->pipes[1], F_GETPIPE_SZ);
  D("SPLICEWRITER: Maximum pipe size set to %d",, maxbytes_inpipe);
#else
  /* Old headers so can't query the size. presume its 64KB */
  maxbytes_inpipe = 65536;
#endif

  sp->pagesize = sysconf(_SC_PAGE_SIZE);
  /* Ok lets try to align with physical page size */
  //sp->pagesize = 65536;
  sp->max_pipe_length = maxbytes_inpipe / sp->pagesize;

#if(DEBUG_OUTPUT)
  fprintf(stdout, "SPLICEWRITER: Max pipe size is %d pages with pagesize %d and buffer bytemax %d\n", sp->max_pipe_length, sp->pagesize, maxbytes_inpipe);
#endif
#ifndef IOVEC_SPLIT_TO_IOV_MAX
  sp->iov =(struct iovec*)malloc(sizeof(struct iovec));
#else
  sp->iov = (struct iovec*)malloc(sp->max_pipe_length * sizeof(struct iovec));
#endif
  CHECK_ERR_NONNULL(sp->iov, "Sp iov malloc");

#ifdef USE_FOPEN
  if(ioi->optbits & READMODE)
    sp->fd = fdopen(ioi->fd, "r");
  else
    sp->fd = fdopen(ioi->fd, "w");
#endif

  ioi->extra_param = (void*)sp;

  err = pthread_mutex_init(&sp->pmutex, NULL);
  CHECK_ERR("pmutex init");
  err = pthread_cond_init(&sp->pcond, NULL);
  CHECK_ERR("pcond init");

  sp->pstatus = PSTATUS_RUN;

  err = pthread_create(&(sp->partner), NULL, partnerloop, (void*)ioi);
  CHECK_ERR("partner create");

  return 0;
}

long splice_write(struct recording_entity * re, void * start, size_t count){
  long ret = 0;
#ifdef IOVEC_SPLIT_TO_IOV_MAX
  void * point_to_start=start;
  long trycount;
#endif
  //int err = 0;
  int i=0;
  off_t oldoffset;
  //int nr_segs;
  long total_w =0;
  struct common_io_info * ioi = (struct common_io_info*) re->opt;
  struct splice_ops *sp = (struct splice_ops *)ioi->extra_param;
  D("SPLICEWRITER: Issuing write of %lu to %s",, count, ioi->curfilename);
  //LOG("SPLICEWRITER: Issuing write of %lu to %s start: %lu\n", count, ioi->curfilename, (unsigned long)start);
#ifndef IOVEC_SPLIT_TO_IOV_MAX
  sp->iov->iov_base = start;
  sp->iov->iov_len = count;
#else
  struct iovec * iov;
#endif

  /* Get current file offset. Used for writeback */
  oldoffset = lseek(ioi->fd, 0, SEEK_CUR);
  ret = count;
  /* TODO: Bidirectional
     if(ioi->read)
     ret = splice_all(ioi->fd, start, count);
     else
     ret = splice_all(start, ioi->fd, count);
     */

#ifdef DISABLE_WRITE
  total_w = count;
#else 
  while(count >0){
#ifdef IOVEC_SPLIT_TO_IOV_MAX
    point_to_start = start;
    trycount = count;
    iov  = sp->iov;
    i=0;
    /* Split the request into page aligned chunks 	*/
    /* TODO: find out where the max size is defined.	*/
    /* Currently splice can handle 16*pagesize 		*/
    while(trycount>0 && i < sp->max_pipe_length && i < IOV_MAX){
      iov->iov_base = point_to_start; 
      /* TODO: Might need to set this only in init 	*/
      iov->iov_len = sp->pagesize;
      iov += 1;
      trycount-=sp->pagesize;
      point_to_start += sp->pagesize;
      /*  Check that we don't go negative on the count */
      /* TODO: This will break if buffer entities aren't pagesize aligned */
      if(trycount>=0)
	i++;
    }
#else
    i=1;
#endif  /* IOVEC_SPLIT_TO_IOV_MAX */

    /* NOTE: SPLICE_F_MORE not *yet* used on vmsplice */
    if(ioi->opt->optbits & READMODE)
    {
    }
    else
      ret = vmsplice(sp->pipes[1], sp->iov, i, SPLICE_F_GIFT|SPLICE_F_MORE);

    if(ret <0){
      fprintf(stderr, "SPLICEWRITER: vmsplice failed for %i elements and %d bytes\n", i, i*(sp->pagesize));
      break;
    }

    if(ioi->opt->optbits & READMODE){
      ret = vmsplice(sp->pipes[0], sp->iov, i, SPLICE_F_GIFT|SPLICE_F_MORE);
    }
    else
    {
#ifdef USE_FOPEN
      ret = splice(sp->pipes[0], NULL, fileno(sp->fd), NULL, ret, SPLICE_F_MOVE|SPLICE_F_MORE);
#else
      ret = splice(sp->pipes[0], NULL, ioi->fd, NULL, ret, SPLICE_F_MOVE|SPLICE_F_MORE);
#endif
    }

    if(ret<0){
      fprintf(stderr, "SPLICEWRITER: Splice failed for %ld bytes on fd %d\n", ret,ioi->fd);
      break;
    }
    start += ret;
    count -= ret;
#ifndef IOVEC_SPLIT_TO_IOV_MAX
    sp->iov->iov_base += ret;
    sp->iov->iov_len = count;
#endif

    // Update statistics 
    total_w += ret;
  }
  if(ret <0){
    perror("SPLICEWRITER: Error on write/read");
    fprintf(stderr, "SPLICEWRITER: Error happened on %s with start %lu and count %lu\n", ioi->curfilename, (long)start, count);
    //return total_w;
    return -1;
  }
  /* Having bad performance. Linus recommends this stuff  at 			*/
  /* http://lkml.indiana.edu/hypermail/linux/kernel/1005.2/01845.html 		*/
  /* and http://lkml.indiana.edu/hypermail/linux/kernel/1005.2/01953.html 	*/

  /* WEIRD: When I dont call sync_file_range and posix_fadvise the log shows 	*/
  /* that the receive buffers don't go to full. Speed is low at about 3Gb/s 	*/
  /* When both are called, speed goes to 5Gb/s and buffer fulls are logged	*/

  if(ioi->opt->optbits & READMODE){
    ret = posix_fadvise(ioi->fd, oldoffset, total_w, POSIX_FADV_NOREUSE|POSIX_FADV_DONTNEED);
  }
  else{
    ret = sync_file_range(ioi->fd,oldoffset,total_w, SYNC_FILE_RANGE_WAIT_BEFORE|SYNC_FILE_RANGE_WRITE|SYNC_FILE_RANGE_WAIT_AFTER);  
    if(ret>=0){
#if(DEBUG_OUTPUT) 
      fprintf(stdout, "SPLICEWRITER: Write done for %lu in %d loops\n", total_w, i);
#endif
    }
    else{
      perror("splice sync");
      fprintf(stderr, "Sync file range failed on %s, with err %s\n", ioi->curfilename, strerror(ret));
      return ret;
    }
    ret = posix_madvise(start,count,POSIX_MADV_SEQUENTIAL|POSIX_MADV_WILLNEED);
  }
#endif /* DISABLE_WRITE */

  ioi->bytes_exchanged += total_w;
#if(DAEMON)
  //if (pthread_spin_lock((ioi->opt->augmentlock)) != 0)
  //E("augmentlock");
  ioi->opt->bytes_exchanged += total_w;
  //if(pthread_spin_unlock((ioi->opt->augmentlock)) != 0)
  //E("augmentlock");
#endif
  //ioi->opt->bytes_exchanged += total_w;
  return total_w;
}
int splice_get_w_fflags(){
  //return O_WRONLY|O_DIRECT|O_NOATIME;
  return O_WRONLY|O_NOATIME;
}
int splice_get_r_fflags(){
  //return O_RDONLY|O_DIRECT|O_NOATIME;
  return O_RDONLY|O_NOATIME;
}
int splice_close(struct recording_entity *re, void *stats){
  int err;
  struct common_io_info * ioi = (struct common_io_info*)re->opt;
  struct splice_ops * sp = (struct splice_ops*)ioi->extra_param;
  err = pthread_mutex_lock(&(sp->pmutex));
  if(err != 0)
    E("Error in mutex lock");
  sp->pstatus = PSTATUS_END;
  err = pthread_cond_signal(&sp->pcond);
  if(err != 0)
    E("error in signal");
  err = pthread_mutex_unlock(&sp->pmutex);
  if(err != 0)
    E("Error in mutex unlock");
  err = pthread_join(sp->partner, NULL);
  if(err != 0)
    E("Error in partner join");
  close(sp->pipes[0]);
  close(sp->pipes[1]);
  free(sp->iov);
  free(ioi->extra_param);
  return common_close(re,stats);
}

int splice_init_splice(struct opt_s *opt, struct recording_entity *re){

  common_init_common_functions(opt,re);
  /*
     re->write_index_data = common_write_index_data;
     re->get_n_packets = common_nofpacks;
     re->get_packet_index = common_pindex;
     re->get_filename = common_wrt_get_filename;
     */

  re->init = init_splice;
  re->close = splice_close;
  re->write = splice_write;
  re->get_r_flags = splice_get_r_fflags;
  re->get_w_flags = splice_get_w_fflags;

  return re->init(opt,re);
}
