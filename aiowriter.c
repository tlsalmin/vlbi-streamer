#include <libaio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/uio.h>

#include "aiowriter.h"
#include "aioringbuf.h"
#include "streamer.h"

#define MAX_EVENTS 128
#ifdef IOVEC
#define IOVEC_MAX 2
#define MAX_IOCB 2
#else
#define MAX_IOCB MAX_EVENTS
#endif
//Nanoseconds for waiting on busy io
#define TIMEOUT_T 100
struct io_info{
  io_context_t ctx;
  char *filename;
  int fd;
  int taken;
  long long offset;
  void * iostruct;
  unsigned long bytes_written;
  int latest_write_num;
  int f_flags;
  loff_t fallocate;
  //struct rec_point * rp;
};

//TODO: Error handling

/* Fatal error handler */
static void io_error(const char *func, int rc)
{
    fprintf(stderr, "%s: error %d", func, rc);
}
int init_recpoint(struct rec_point *rp, struct opt_s *opt){
}

static void wr_done(io_context_t ctx, struct iocb *iocb, long res, long res2){
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

int aiow_init(struct recording_entity *re){
  int ret;
  //void * errpoint;
  re->opt = (void*)malloc(sizeof(struct io_info));
  struct io_info * ioi = (struct io_info *) re->opt;
  struct stat statinfo;
  int err =0;

  ioi->latest_write_num = 0;
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "STREAMER: Initializing write point\n");
#endif
  //Check if file exists
  ioi->f_flags = O_WRONLY|O_DIRECT|O_NOATIME|O_NONBLOCK;
  err = stat(ioi->filename, &statinfo);
  if (err < 0) 
    if (errno == ENOENT){
      ioi->f_flags |= O_CREAT;
      err = 0;
    }


  //This will overwrite existing file.TODO: Check what is the desired default behaviour 
  ioi->fd = open(ioi->filename, ioi->f_flags, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
  if(ioi->fd == -1){
    fprintf(stderr,"Error %s on %s\n",strerror(errno), ioi->filename);
    return -1;
  }
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "STREAMER: File opened\n");
#endif
  //TODO: Set offset accordingly if file already exists. Not sure if
  //needed, since data consistency would take a hit anyway
  ioi->offset = 0;
  //RATE = 10 Gb => RATE = 10*1024*1024*1024/8 bytes/s. Handled on n_threads
  //for s seconds.
  loff_t prealloc_bytes = (RATE*opt->time*1024)/(opt->n_threads*8);
  //Split kb/gb stuff to avoid overflow warning
  prealloc_bytes = prealloc_bytes*1024*1024;
  //set flag FALLOC_FL_KEEP_SIZE to precheck drive for errors
  err = fallocate(rp->fd, 0,0, prealloc_bytes);
  if(err == -1){
    fprintf(stderr, "Fallocate failed on %s", rp->filename);
    return err;
  }
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "STREAMER: File preallocated\n");
#endif
  //Uses AIOWRITER atm. TODO: Make really generic, so you can change the backends
  //aiow_init((void*)&(spec_ops->rbuf), (void*)spec_ops->rp);
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "STREAMER: AIOW initialized\n");
#endif
  return err;

  rp->bytes_written = 0;
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Memaligning buffer\n");
#endif
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Preparing iostructs\n");
#endif
  rp->iostruct = (void*)malloc(sizeof(struct io_info));
  if(rp->iostruct == NULL){
    perror("io_struct malloc");
    return -1;
  }
  struct io_info * ioi = (struct io_info*)rp->iostruct;
  errpoint = memset(&(ioi->ctx), 0, sizeof(ioi->ctx));
  if(errpoint == NULL){
    perror("Memset ctx");
    return -1;
  }
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Queue init\n");
#endif
  ret = io_queue_init(MAX_EVENTS, &(ioi->ctx));
  if(ret < 0){
    perror("IO_QUEUE_INIT");
    return -1;
  }
  return ret;
}
int aiow_write(void * ringbuf, void * recpoint, int diff){
  int ret, i;
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Performing write\n");
#endif
  struct ringbuf * rbuf = (struct ringbuf *)ringbuf;
  struct rec_point * rp = (struct rec_point *)recpoint;
  struct io_info * ioi = (struct io_info * )rp->iostruct;
  rp->latest_write_num = diff;
#ifdef IOVEC
  int vecs;
#endif
  int requests = 1+((rbuf->writer_head < rbuf->hdwriter_head) && rbuf->writer_head > 0);
  rbuf->ready_to_write -= requests;
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Blocking writes. Write from %i to %i diff %i elems %i\n", rbuf->hdwriter_head, rbuf->writer_head, diff, rbuf->num_elems);
#endif

#ifdef IOVEC
  //ioi->iocbpp[0] = (struct iocb *) malloc(sizeof(struct iocb));
#else
  struct iocb *ib[requests];
  for(i=0;i<requests;i++){
    void * start;
    size_t count;
    int endi;
    ib[i] = (struct iocb *) malloc(sizeof(struct iocb));
    //Only special case if we're going over the buffer end
    if(i == 0){
      start = rbuf->buffer + (rbuf->hdwriter_head * rbuf->elem_size);
      if(requests ==2){
	endi = rbuf->num_elems - rbuf->hdwriter_head;
	diff -= endi;
      }
      else
	endi = diff;
    }
    else{
      start = rbuf->buffer;
      endi = diff;
    }
    count = (endi) * (rbuf->elem_size);
    io_prep_pwrite(ib[i], rp->fd, start, count, rp->offset);
    io_set_callback(ib[i], wr_done);
#ifdef DEBUG_OUTPUT
    fprintf(stdout, "AIOW: Prepared write for %lu bytes\n", count);
#endif
  } 
#endif
  /*
     if(ioi->iocbpp[0] == NULL){
     perror("malloc iocbpp");
     return -1;
     }
     */
#ifdef IOVEC
  struct iovec iov[IOVEC_MAX];
  gen_iov(rbuf, &vecs, &iov);
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Generated write vectors\n");
#endif
#endif

#ifdef IOVEC
  io_prep_pwritev(ioi->iocbpp[0], rp->fd, iov, vecs, rp->offset);
  io_set_callback(ioi->iocbpp[0], wr_done);
#endif


  //Not sure if 3rd argument safe, but running 
  //one iocb at a time anyway
#ifdef IOVEC
  ret = io_submit(ioi->ctx, 1, ioi->iocbpp);
#else
  ret = io_submit(ioi->ctx, requests, ib);
#endif
#ifdef DEBUG_OUTPUT
  if(ret > 0)
    fprintf(stdout, "AIOW: Submitted %d writes\n", ret);
  else{
    perror("io_submit");
    return -1;
  }
#endif
  //free(iov);
  return requests;
}
int aiow_check(void * rep, void * rib){
  struct rec_point * rp = (struct rec_point*) rep;
  struct ringbuf * rb = (struct ringbuf *)rib;
  //Just poll, so we can keep receiving more packets
  struct io_info * ioi = (struct io_info *)rp->iostruct;
  static struct timespec timeout = { 0, 0 };
  struct io_event event;
  int ret = io_getevents(ioi->ctx, 0, 1, &event, &timeout);
  //
  if(ret > 0){
    if(rp->latest_write_num > 0){
      increment_amount(rb, &(rb->tail), rp->latest_write_num);
      rp->latest_write_num = 0;
    }
    rp->bytes_written += event.res;
#ifdef DEBUG_OUTPUT
    fprintf(stdout, "AIOW: Check return %d\n", ret);
#endif
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
int aiow_wait_for_write(struct rec_point* rp){
  //struct rec_point * rp = (struct rec_point *) recpoint;
  struct io_info * ioi = (struct io_info *)rp->iostruct;
  //Needs to be static so ..durr
  static struct timespec timeout = { 0, TIMEOUT_T };
  //Not sure if this works, since io_queue_run doesn't
  //work (have to use io_getevents), or then I just
  //don't know how to use it
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Buffer full. Going to sleep\n");
#endif
  return io_queue_wait(ioi->ctx, &timeout);
}
/*
void aiow_write_done(){
}
*/
int aiow_close(void * ioinfo){
  struct io_info * ioi = (struct io_info*)ioinfo;
  io_destroy(ioi->ctx);
  //free(ioi->iocbpp);
  //Malloced in this file, so freeing here too
  //free(rbuf->buffer);
  free(ioi);
  return 0;
}
int aiow_init_rec_entity(struct opt_s * opt, struct recording_entity * re){
  //TODO
}
