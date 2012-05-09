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
#if(DEBUG_OUTPUT)
#include <time.h>
#endif

#include "aiowriter.h"
#include "streamer.h"
#include "common_wrt.h"

#define MAX_EVENTS 128
#ifdef IOVEC
#define IOVEC_MAX 2
#define MAX_IOCB 2
#else
#define MAX_IOCB MAX_EVENTS
#endif
//Nanoseconds for waiting on busy io
#define TIMEOUT_T 100
/*
struct io_info{
  io_context_t * ctx;
};
*/


/* Fatal error handler */
/*
static void wr_done(io_context_t ctx, struct iocb *iocb, long res, long res2){
  fprintf(stdout, "This will never make it to print\n");
  if(res2 != 0)
    io_error("aio write", res2);
  if(res != iocb->u.c.nbytes){
    fprintf(stderr, "write missed bytes expect %lu got %li", iocb->u.c.nbytes, res2);
  }
#if(DEBUG_OUTPUT)
  fprintf(stdout, "Write callback done. Wrote %li bytes\n", res);
#endif
  free(iocb);
}
*/
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

#if(DEBUG_OUTPUT)
  fprintf(stdout, "AIOW: Preparing iostructs\n");
#endif
  //ib[0] = (struct iocb*) malloc(sizeof(struct iocb));
  ioi->extra_param = (void*) malloc(sizeof(struct extra_parameters));
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
#if(DEBUG_OUTPUT)
  fprintf(stdout, "AIOW: Queue init\n");
#endif
  ret = io_queue_init(MAX_EVENTS, &(ep->ctx));
  if(ret < 0){
    perror("AIOW: IO_QUEUE_INIT");
    return -1;
  }
  return 0;
}
int aiow_get_w_fflags(){
    return  O_WRONLY|O_DIRECT|O_NOATIME|O_NONBLOCK;
    //return  O_WRONLY|O_DIRECT|O_NOATIME;
}
int aiow_get_r_fflags(){
    return  O_RDONLY|O_DIRECT|O_NOATIME|O_NONBLOCK;
    //return  O_RDONLY|O_DIRECT|O_NOATIME;
}

long aiow_write(struct recording_entity * re, void * start, size_t count){
  long ret;
#if(DEBUG_OUTPUT)
  fprintf(stdout, "AIOW: Performing read/write\n");
#endif

  struct common_io_info * ioi = (struct common_io_info * )re->opt;
  struct extra_parameters * ep = (struct extra_parameters*)ioi->extra_param;

  if(ep->used_events < MAX_EVENTS){
  //struct iocb *ib[1];
  //ib[0] = (struct iocb*) malloc(sizeof(struct iocb));
    if(ioi->optbits & READMODE)
      io_prep_pread(&(ep->ib[ep->i]), ioi->fd, start, count, ioi->offset);
    else
      io_prep_pwrite(&(ep->ib[ep->i]), ioi->fd, start, count, ioi->offset);
  }
  else{
#if(DEBUG_OUTPUT)
    fprintf(stdout, "AIOWRITER: Requests full! Returning 0\n");
#endif
    //TODO: IOwait or sleep
    return 0;
    }

  //io_set_callback(ib[0], wr_done);

#if(DEBUG_OUTPUT)
  fprintf(stdout, "AIOW: Prepared read/write for %lu bytes\n", count);
#endif
  struct iocb * ibi[1];
  ibi[0] = &(ep->ib[ep->i]);

  //Not sure if 3rd argument safe, but running 
  //one iocb at a time anyway
  ret = io_submit(*((io_context_t*)(ioi->extra_param)), 1, ibi);
  ep->used_events++;
  ep->i = (ep->i + 1)%MAX_EVENTS;

#if(DEBUG_OUTPUT)
  fprintf(stdout, "AIOW: Submitted %ld reads/writes\n", ret);
#endif
  if(ret <0){
    /* an errno == 0 means that the submit just failed. 	*/
    /* This is probably due to too many requests pending 	*/
    /* Just return 0 so the thread doesn't shut down		*/
    perror("AIOW: io_submit");
    fprintf(stdout, "perror number %d\n", errno);
    if(errno == 0)
      return 0;
    else
      return -1;
  }
  ioi->offset += count;
  //ioi->bytes_exchanged += count;
  if(!(aiow_get_r_fflags() & O_NONBLOCK))
    ioi->bytes_exchanged+=count;
  return count;
}
long aiow_check(struct recording_entity * re,int tout){
  //Just poll, so we can keep receiving more packets
  struct common_io_info * ioi = (struct common_io_info *)re->opt;
  long ret;
  static struct timespec timeout = { 0, 0 };
  static struct timespec rtout = { 1, TIMEOUT_T };
  struct io_event event;
  struct extra_parameters *ep = (struct extra_parameters *)ioi->extra_param;
  //io_context_t * ctx = ep->ctx;
  if(tout == 1){
    D("Timeout set on check for %s",, ioi->filename);
    ret = io_getevents(ep->ctx, 0, 1, &event, &rtout);
    D("Released on %s",, ioi->filename);
  }
  else
    ret = io_getevents(ep->ctx, 0, 1, &event, &timeout);
  //
  if(ret > 0){
    ep->used_events-=ret;
    if((signed long )event.res > 0){
      ioi->bytes_exchanged += event.res;
      ret = event.res;
#if(DEBUG_OUTPUT)
      fprintf(stdout, "AIOW: Check return %ld, read/written %lu bytes\n", ret, event.res);
#endif
    }
    else{
      if(errno == 0){
#if(DEBUG_OUTPUT)
	fprintf(stdout, "AIOWRITER: end of file! event.red: %ld  %d\n", event.res, errno);
#endif
	return 1;//event.res;
      }
      else{
	fprintf(stderr, "AIOW: Write check return error %ld\n", event.res);
	perror("AIOW: Check");
	return -1;
      }
    }

  }
  /*
  else{
#if(DEBUG_OUTPUT)
    fprintf(stdout, "AIOW: Check: No writes done\n");
#endif
  }
  */

  /*
   * TODO: Change implementation for reads also
   * Might need a unified writer backend to
   * queue reads properly, thought reads
   * go to different file
   */

  return ret;
}
//Not used, since can't update status etc.
//Using queue-stuff instead
//TODO: Make proper sleep. io_queue_wait doesn't work
int aiow_wait_for_write(struct recording_entity* re){
  //struct rec_point * rp = (struct rec_point *) recpoint;
  struct common_io_info * ioi = (struct common_io_info *)re->opt;
  struct extra_parameters *ep = (struct extra_parameters*)ioi->extra_param;
  //Needs to be static so ..durr
  static struct timespec timeout = { 1, TIMEOUT_T };
  //Not sure if this works, since io_queue_run doesn't
  //work (have to use io_getevents), or then I just
  //don't know how to use it
  D("AIOW: Buffer full %s. Going to sleep\n",, ioi->filename);
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

