/*
 * aiowriter.c -- Asynchronius writer for vlbi-streamer
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
#include <libaio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include "config.h"
#include <time.h>
#include <time.h>

#include "aiowriter.h"
#include "streamer.h"
#include "common_wrt.h"

extern FILE* logfile;

#define MAX_EVENTS 128
#ifdef IOVEC
#define IOVEC_MAX 2
#define MAX_IOCB 2
#else
#define MAX_IOCB MAX_EVENTS
#endif
//Nanoseconds for waiting on busy io
#define TIMEOUT_T 100

struct extra_parameters{
  io_context_t ctx;
  struct iocb ib[MAX_EVENTS];
  int used_events;
  int i;
};
//Read init is so similar to write, that i'll just add a parameter
int aiow_init(struct opt_s* opt, struct recording_entity *re){
  int ret;
  struct extra_parameters * ep;

  ret = common_w_init(opt,re);
  if(ret!=0){
    fprintf(stderr, "Common w init returned error %d\n", ret);
    return ret;
  }

  struct common_io_info * ioi = (struct common_io_info *) re->opt;

  D("Preparing iostructs");
  //ib[0] = (struct iocb*) malloc(sizeof(struct iocb));
  ioi->extra_param = (void*) malloc(sizeof(struct extra_parameters));
  CHECK_ERR_NONNULL(ioi->extra_param, "Malloc extra params");
  ep = (struct extra_parameters *) ioi->extra_param;
  ep->used_events = 0;
  memset(&(ep->ib), 0,sizeof(struct iocb)*MAX_EVENTS);
  ep->i = 0;
  //ioi->extra_param =(io_context_t *) malloc(sizeof(io_context_t));
  void * errpoint = memset((void*)&(ep->ctx), 0, sizeof(io_context_t));
  if(errpoint== NULL){
    perror("AIOW: Memset ctx");
    return -1;
  }
  //io_context_t* ctx = (io_context_t*)ioi->extra_param;
  D("Queue init");
  ret = io_queue_init(MAX_EVENTS, &(ep->ctx));
  if(ret < 0){
    perror("AIOW: IO_QUEUE_INIT");
    return -1;
  }
  return 0;
}
int aiow_get_w_fflags(){
    return  O_WRONLY|O_DIRECT|O_NOATIME|O_NONBLOCK;
    //return  O_WRONLY|O_NOATIME|O_NONBLOCK;
    //return  O_WRONLY|O_DIRECT|O_NOATIME;
}
int aiow_get_r_fflags(){
    return  O_RDONLY|O_DIRECT|O_NOATIME|O_NONBLOCK;
    //return  O_RDONLY|O_DIRECT|O_NOATIME;
}
long aiow_write(struct recording_entity * re,void * start,size_t count){
  long ret;
  D("AIOW: Performing read/write");

  struct common_io_info * ioi = (struct common_io_info * )re->opt;
  struct extra_parameters * ep = (struct extra_parameters*)ioi->extra_param;

  if(ep->used_events < MAX_EVENTS){
  //struct iocb *ib[1];
  //ib[0] = (struct iocb*) malloc(sizeof(struct iocb));
    if(ioi->opt->optbits & READMODE)
      io_prep_pread(&(ep->ib[ep->i]), ioi->fd, start, count, ioi->offset);
    else
      io_prep_pwrite(&(ep->ib[ep->i]), ioi->fd, start, count, ioi->offset);
  }
  else
    {
      D("AIOWRITER: Requests full! Returning 0");
      //TODO: IOwait or sleep
      return 0;
    }

  //io_set_callback(ib[0], wr_done);

  D("AIOW: Prepared read/write for %lu bytes", count);
  struct iocb * ibi[1];
  ibi[0] = &(ep->ib[ep->i]);

  //Not sure if 3rd argument safe, but running 
  //one iocb at a time anyway
  ret = io_submit(*((io_context_t*)(ioi->extra_param)), 1, ibi);
  ep->used_events++;
  ep->i = (ep->i + 1)%MAX_EVENTS;

  D("AIOW: Submitted %ld reads/writes", ret);
  if(ret <0){
    /* an errno == 0 means that the submit just failed. 	*/
    /* This is probably due to too many requests pending 	*/
    /* Just return 0 so the thread doesn't shut down		*/
    perror("AIOW: io_submit");
    fprintf(stdout, "perror number %d\n", errno);
    /*
    if(errno == 0)
      return 0;
    else
    */
      return -1;
  }
  ioi->offset += count;
  if(!(aiow_get_r_fflags() & O_NONBLOCK)){
    ioi->bytes_exchanged+=count;
#if(DAEMON)
    //pthread_spin_lock((ioi->opt->augmentlock));
    ioi->opt->bytes_exchanged += count;
    //pthread_spin_unlock((ioi->opt->augmentlock));
#endif
    //ioi->opt->bytes_exchanged+=count;
  }
  return count;
}
long aiow_check(struct recording_entity * re,int tout){
  //Just poll, so we can keep receiving more packets
  struct common_io_info * ioi = (struct common_io_info *)re->opt;
  //errno=0;
  //perror("fcntl");
  //E("Dat fcntl %d errno %d", fcntl(ioi->fd, F_GETFL), errno);
  //perror("fcntl");
  long ret;
  static struct timespec timeout = { 0, 0 };
  static struct timespec rtout = { 1, TIMEOUT_T };
  struct io_event event;
  struct extra_parameters *ep = (struct extra_parameters *)ioi->extra_param;
  //io_context_t * ctx = ep->ctx;
  if(tout == 1){
    D("Timeout set on check for %s", ioi->curfilename);
    ret = io_getevents(ep->ctx, 0, 1, &event, &rtout);
    D("Released on %s", ioi->curfilename);
  }
  else{
    ret = io_getevents(ep->ctx, 0, 1, &event, &timeout);
  }
  if(ret > 0){
    ep->used_events-=ret;
    if((signed long )event.res > 0){
      ioi->bytes_exchanged += event.res;
#if(DAEMON)
      //pthread_spin_lock((ioi->opt->augmentlock));
      ioi->opt->bytes_exchanged += event.res;
      //pthread_spin_unlock((ioi->opt->augmentlock));
#endif
      ret = event.res;
      D("Check return %ld, read/written %lu bytes", ret, event.res);
    }
    else{
      if(errno == 0 && event.res == 0){
	D("end of file! event.red: %ld  %d", event.res, errno);
	perror("AIOW: Check");
	//return -1;//event.res;
	return AIO_END_OF_FILE;
      } 
      else{
	E("Write check return error event.res: %ld, fd: %d,  file: %s", event.res, ioi->fd, ioi->curfilename);
	if(ioi->opt->optbits & READMODE)
	  E("Happened in readmode");
	perror("AIOW: Check");
	return -1;
      }
    }
  }
  else if (ret < 0){
    E("Error in aio check");
    perror("AIOW:check");
    return ret;
  }
  return ret;
}
//Not used, since can't update status etc.
//Using queue-stuff instead
//TODO: Make proper sleep. io_queue_wait doesn't work
int aiow_wait_for_write(struct recording_entity* re){
  //struct rec_point * rp = (struct rec_point *) recpoint;
  struct common_io_info * ioi = (struct common_io_info *)re->opt;
  //struct extra_parameters *ep = (struct extra_parameters*)ioi->extra_param;
  //Needs to be static so ..durr
  //static struct timespec timeout = { 1, TIMEOUT_T };
  //Not sure if this works, since io_queue_run doesn't
  //work (have to use io_getevents), or then I just
  //don't know how to use it
  D("Buffer full %s. Going to sleep\n", ioi->curfilename);
  //Doesn't really sleep :/
  //return io_queue_wait(ep->ctx, &timeout);
  return usleep(5000);
}
int aiow_close(struct recording_entity * re, void * stats){
  struct common_io_info * ioi = (struct common_io_info*)re->opt;
  struct extra_parameters *ep = (struct extra_parameters*)ioi->extra_param;

  io_destroy(ep->ctx);
  free(ep);
  common_close(re, stats);
  /* Done in common_close */
  //free(ioi);

  return 0;
}
/*
 * Helper function for initializing a recording_entity
 */
int aiow_init_rec_entity(struct opt_s * opt, struct recording_entity * re){
  /*
  re->write_index_data = common_write_index_data;
  re->get_n_packets = common_nofpacks;
  re->get_packet_index = common_pindex;
  re->get_filename = common_wrt_get_filename;
  re->getfd = common_getfd;
  */
  common_init_common_functions(opt,re);
  re->init = aiow_init;
  re->write = aiow_write;
  re->wait = aiow_wait_for_write;
  re->close = aiow_close;
  re->check = aiow_check;
  re->get_r_flags = aiow_get_r_fflags;
  re->get_w_flags = aiow_get_w_fflags;

  return re->init(opt,re);
}

