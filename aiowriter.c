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
#ifdef DEBUG_OUTPUT
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
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "Write callback done. Wrote %li bytes\n", res);
#endif
  free(iocb);
}
*/
//Read init is so similar to write, that i'll just add a parameter
int aiow_init(struct opt_s* opt, struct recording_entity *re){
  int ret;

  ret = common_w_init(opt,re);
  if(ret<0)
    return -1;

  struct common_io_info * ioi = (struct common_io_info *) re->opt;

#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Preparing iostructs\n");
#endif
  ioi->extra_param =(io_context_t *) malloc(sizeof(io_context_t));
  void * errpoint = memset((void*)ioi->extra_param, 0, sizeof(io_context_t));
  if(errpoint== NULL){
    perror("AIOW: Memset ctx");
    return -1;
  }
  io_context_t* ctx = (io_context_t*)ioi->extra_param;
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Queue init\n");
#endif
  ret = io_queue_init(MAX_EVENTS, ctx);
  if(ret < 0){
    perror("AIOW: IO_QUEUE_INIT");
    return -1;
  }
  return ret;
}

int aiow_write(struct recording_entity * re, void * start, size_t count){
  int ret;
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Performing read/write\n");
#endif

  struct common_io_info * ioi = (struct common_io_info * )re->opt;

  struct iocb *ib[1];
  ib[0] = (struct iocb*) malloc(sizeof(struct iocb));
  if(ioi->read == 1)
    io_prep_pread(ib[0], ioi->fd, start, count, ioi->offset);
  else
    io_prep_pwrite(ib[0], ioi->fd, start, count, ioi->offset);

  //io_set_callback(ib[0], wr_done);

#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Prepared read/write for %lu bytes\n", count);
#endif

  //Not sure if 3rd argument safe, but running 
  //one iocb at a time anyway
  ret = io_submit(*((io_context_t*)(ioi->extra_param)), 1, ib);

#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Submitted %d reads/writes\n", ret);
#endif
  if(ret <0){
    perror("AIOW: io_submit");
    return -1;
  }
  ioi->offset += count;
  //ioi->bytes_exchanged += count;
  return ret;
}
int aiow_check(struct recording_entity * re){
  //Just poll, so we can keep receiving more packets
  struct common_io_info * ioi = (struct common_io_info *)re->opt;
  static struct timespec timeout = { 0, 0 };
  struct io_event event;
  io_context_t * ctx = (io_context_t*)ioi->extra_param;
  int ret = io_getevents(*(ctx), 0, 1, &event, &timeout);
  //
  if(ret > 0){
    if((signed long )event.res > 0){
    ioi->bytes_exchanged += event.res;
    ret = event.res;
#ifdef DEBUG_OUTPUT
    fprintf(stdout, "AIOW: Check return %d, read/written %lu bytes\n", ret, event.res);
#endif
    }
    else{
      fprintf(stderr, "AIOW: Write check return error %ld\n", event.res);
      perror("AIOW: Check");
      return -1;
    }
  }

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
  //Needs to be static so ..durr
  //static struct timespec timeout = { 1, TIMEOUT_T };
  //Not sure if this works, since io_queue_run doesn't
  //work (have to use io_getevents), or then I just
  //don't know how to use it
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Buffer full %s. Going to sleep\n", ioi->filename);
#endif
  //Doesn't really sleep :/
  //return io_queue_wait(*(ioi->ctx), &timeout);
  return usleep(5000);
}
int aiow_close(struct recording_entity * re, void * stats){
  struct common_io_info * ioi = (struct common_io_info*)re->opt;

  io_destroy(*(io_context_t*)ioi->extra_param);

  common_close(re, stats);
  return 0;
}
/*
 * Helper function for initializing a recording_entity
 */
int aiow_init_rec_entity(struct opt_s * opt, struct recording_entity * re){
  re->init = aiow_init;
  re->write = aiow_write;
  re->wait = aiow_wait_for_write;
  re->close = aiow_close;
  re->check = aiow_check;
  re->write_index_data = common_write_index_data;
  re->get_n_packets = common_nofpacks;
  re->get_packet_index = common_pindex;

  return re->init(opt,re);
}
int aiow_init_dummy(struct opt_s *op , struct recording_entity *re){
  /*
  re->init = null;
  re-write = dummy_write_wrapped;
  */
  return 1;
}
const char * aiow_get_filename(struct recording_entity *re){
  return ((struct common_io_info *)re->opt)->filename;
}

