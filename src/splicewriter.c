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
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h> /* For madvise */
#include <sys/fcntl.h>
//#define _ASM_GENERIC_FCNTL_H
//#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/mman.h> /* For madvise */
//#include <fcntl.h>

#include "config.h"

#include "streamer.h"
#include "common_wrt.h"
#include "splicewriter.h"

extern FILE* logfile;

#define SP_LOCKIT do{ if(pthread_mutex_lock(&(sp->pmutex)) != 0){ sp->pstatus = PSTATUS_ERROR; return NULL; }}while(0)
#define SP_MAINLOCKIT do{ if(pthread_mutex_lock(&(sp->pmutex)) != 0){ sp->pstatus = PSTATUS_ERROR; return -1; }}while(0)

#define SP_UNLOCKIT do {if(pthread_mutex_unlock(&sp->pmutex) != 0){ sp->pstatus = PSTATUS_ERROR; return NULL; }}while(0)
#define SP_MAINUNLOCKIT do {if(pthread_mutex_unlock(&sp->pmutex) != 0){ sp->pstatus = PSTATUS_ERROR; return -1; }}while(0)

#define SP_WAIT do{ if(pthread_cond_wait(&sp->pcond, &sp->pmutex) != 0){ sp->pstatus = PSTATUS_ERROR; return NULL; }}while(0)
#define SP_WAIT_MAIN do{ if(pthread_cond_wait(&sp->pcond, &sp->pmutex) != 0){ sp->pstatus = PSTATUS_ERROR; return -1; }}while(0)

#define SP_SIGNAL do{ if(pthread_cond_signal(&sp->pcond) != 0){ sp->pstatus = PSTATUS_ERROR; return -1; }}while(0)
#define SP_SIGNAL_MAIN do{ if(pthread_cond_signal(&sp->pcond) != 0){ sp->pstatus = PSTATUS_ERROR; return NULL; }}while(0)

//#define PIPE_STUFF_IN_WRITELOOP
#define IOVEC_SPLIT_TO_IOV_MAX
/* Default max in 3.2.12. Larger possible if CAP_SYS_RESOURCE */
#define MAX_PIPE_SIZE 1048576
/* Read a claim that proper scatter gather requires fopen not open */
//#define MAX_IOVEC 16

struct splice_ops{
  struct iovec iov[IOV_MAX];
  int pipes[2];
  unsigned int max_pipe_length;
  int pagesize;
  off_t tosplice;
  pthread_t partner;
  pthread_mutex_t pmutex;
  pthread_cond_t pcond;
  int pstatus;
};
void * partnerloop(void* opts)
{
  struct common_io_info *ioi = (struct common_io_info*)opts;
  struct splice_ops * sp = (struct splice_ops*)ioi->extra_param;
  off_t temp_tosplice;
  off_t left_to_splice;
  long err;
  SP_LOCKIT;
  while(sp->pstatus > PSTATUS_END)
  {
    while(sp->pstatus == PSTATUS_RUN)
      SP_WAIT;
    left_to_splice = sp->tosplice;
    SP_UNLOCKIT;
    D("Got a job to write %ld",, left_to_splice);

    while(left_to_splice > 0)
    {
      temp_tosplice = MIN(left_to_splice, sp->max_pipe_length*sp->pagesize);
      if(ioi->opt->optbits & READMODE)
      {
	err = splice(ioi->fd, NULL, sp->pipes[1], NULL, temp_tosplice, SPLICE_F_MOVE|SPLICE_F_MORE);
      }
      else
      {
	err = splice(sp->pipes[0], NULL, ioi->fd, NULL, temp_tosplice, SPLICE_F_MOVE|SPLICE_F_MORE);
      }
      if(err < 0)
      {
	E("Error in splice partner");
	SP_LOCKIT;
	sp->pstatus = -1;
	SP_UNLOCKIT;
	return NULL;
      }
      else if(err == 0){
	D("Pipe closed");
	break;
      }
      left_to_splice-=err;
      D("Done a loop for %ld left %ld",, err, left_to_splice);
    }
    D("Job done");

    SP_LOCKIT;
    if(sp->pstatus >PSTATUS_END){
      sp->pstatus = PSTATUS_RUN;
      SP_SIGNAL_MAIN;
    }
  }
  D("Stopping splice partner");
  sp->pstatus =PSTATUS_END;
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
  memset(sp,0,sizeof(struct splice_ops));
  CHECK_ERR_NONNULL(sp, "Splice ops malloc");

#ifndef PIPE_STUFF_IN_WRITELOOP
  ret = pipe(sp->pipes);
  if(ret<0)
    return -1;
#endif
  /* TODO: Handle error for pipe size change */
#ifdef F_SETPIPE_SZ
  D("Can change pipesize!");
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
  /*
  sp->iov = (struct iovec*)malloc(IOV_MAX * sizeof(struct iovec));
  CHECK_ERR_NONNULL(sp->iov, "Sp iov malloc");
  */


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

inline unsigned int setup_nvecs(struct splice_ops *sp, void* start, size_t count)
{
  unsigned int i;
  unsigned int n_vecs = MIN(sp->max_pipe_length, count/sp->pagesize);
  int lastwrite;
  for(i=0;i<n_vecs;i++)
  {
    sp->iov[i].iov_base = start+(i*sp->pagesize);
    sp->iov[i].iov_len = sp->pagesize;
  }
  if(n_vecs < sp->max_pipe_length && (lastwrite = (count % sp->pagesize)) != 0)
  {
    sp->iov[n_vecs].iov_base = start+(n_vecs*sp->pagesize);
    sp->iov[n_vecs].iov_len = lastwrite;
    n_vecs++;
  }
  return n_vecs;
}

long splice_write(struct recording_entity * re,void * start, size_t count){
  long ret = 0;
  void * point_to_start=start;
  long trycount = count;
  off_t oldoffset;
  long total_w =0;
  unsigned int n_vecs;
  struct common_io_info * ioi = (struct common_io_info*) re->opt;
  struct splice_ops *sp = (struct splice_ops *)ioi->extra_param;
  //int maxbytes_inpipe = sp->max_pipe_length * sp->pagesize;
  D("SPLICEWRITER: Issuing write of %lu to %s",, count, ioi->curfilename);
  //LOG("SPLICEWRITER: Issuing write of %lu to %s start: %lu\n", count, ioi->curfilename, (unsigned long)start);

  D("Informing partner on write");
  SP_MAINLOCKIT;
  sp->tosplice = count;
  while(sp->pstatus == PSTATUS_JOBTODO)
  {
    D("Partner not ready so waiting");
    SP_WAIT_MAIN;
  }
  if(sp->pstatus == PSTATUS_RUN)
    sp->pstatus = PSTATUS_JOBTODO;
  else{
    E("Partner in err!");
    return -1;
  }
  SP_SIGNAL;
  SP_MAINUNLOCKIT;

  n_vecs = setup_nvecs(sp, start, count);

  D("Starting splice");
  /* Get current file offset. Used for writeback */
  oldoffset = lseek(ioi->fd, 0, SEEK_CUR);

  while(trycount >0 && sp->pstatus > PSTATUS_END){

    if(ioi->opt->optbits & READMODE){
      ret = vmsplice(sp->pipes[0], sp->iov, n_vecs, SPLICE_F_GIFT);
      //ret = splice(sp->pipes[0], NULL, ioi->shmid, NULL, trycount, SPLICE_F_MORE|SPLICE_F_MOVE);
    }
    else
    {
      //ret = splice(ioi->shmid, NULL, sp->pipes[1], NULL, trycount, SPLICE_F_MORE|SPLICE_F_MOVE);
      ret = vmsplice(sp->pipes[1], sp->iov, n_vecs, SPLICE_F_GIFT);
    }

    //D("Done aloop ret %ld count %ld and nvecs %d",, ret, trycount, n_vecs);
    if(ret<0){
      fprintf(stderr, "SPLICEWRITER: Splice failed for %ld bytes on fd %d\n", ret,ioi->fd);
      break;
    }
    else if(ret == 0){
      D("Pipe closed");
      break;
    }
    start += ret;
    trycount -= ret;
    point_to_start += ret;

    total_w += ret;

    if(trycount > 0)
    {
      //if((n_vecs = setup_nvecs(ioi, sp->iov, point_to_start, trycount)) < 0 ){
      n_vecs = setup_nvecs(sp, point_to_start, trycount);
    }
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

  /*
  if(ioi->opt->optbits & READMODE){
    ret = posix_fadvise(ioi->fd, oldoffset, total_w, POSIX_FADV_NOREUSE|POSIX_FADV_DONTNEED);
    if(posix_madvise(start,count,POSIX_MADV_SEQUENTIAL|POSIX_MADV_WILLNEED) != 0)
      E("Error in posix_madvise");
  }
  else{
    if(sync_file_range(ioi->fd,oldoffset,total_w, SYNC_FILE_RANGE_WAIT_BEFORE|SYNC_FILE_RANGE_WRITE|SYNC_FILE_RANGE_WAIT_AFTER)   != 0)
      E("splice sync");
    if(posix_madvise(start,count,POSIX_MADV_DONTNEED) != 0)
      E("Error in posix_madvise");
  }
  */

#if(DEBUG_OUTPUT) 
      fprintf(stdout, "SPLICEWRITER: Write done for %lu\n", total_w);
#endif
  ioi->bytes_exchanged += total_w;
#if(DAEMON)
  //if (pthread_spin_lock((ioi->opt->augmentlock)) != 0)
  //E("augmentlock");
  ioi->opt->bytes_exchanged += total_w;
  //if(pthread_spin_unlock((ioi->opt->augmentlock)) != 0)
  //E("augmentlock");
#endif
  SP_MAINLOCKIT;
  while(sp->pstatus == PSTATUS_JOBTODO)
    SP_WAIT_MAIN;
  if(sp->pstatus == PSTATUS_ERROR)
  {
    E("partner ended in error");
    total_w = -1;
  }
  SP_MAINUNLOCKIT;
  
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
  //free(sp->iov);
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
