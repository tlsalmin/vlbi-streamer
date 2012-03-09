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

#include "aiowriter.h"
//#include "aioringbuf.h"
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
  io_context_t * ctx;
  char *filename;
  int fd;
  long long offset;
  unsigned long bytes_written;
  int f_flags;
};

//TODO: Error handling

/* Fatal error handler */
static void io_error(const char *func, int rc)
{
    fprintf(stderr, "%s: error %d", func, rc);
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

int aiow_init(struct opt_s* opt, struct recording_entity *re){
  int ret;
  //void * errpoint;
  re->opt = (void*)malloc(sizeof(struct io_info));
  struct io_info * ioi = (struct io_info *) re->opt;
  struct stat statinfo;
  int err =0;

  //ioi->latest_write_num = 0;
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Initializing write point\n");
#endif
  //Check if file exists
  ioi->f_flags = O_WRONLY|O_DIRECT|O_NOATIME|O_NONBLOCK;
  ioi->filename = opt->filenames[opt->taken_rpoints++];
  err = stat(ioi->filename, &statinfo);
  if (err < 0) {
    if (errno == ENOENT){
#ifdef DEBUG_OUTPUT
      fprintf(stdout, "File doesn't exist. Creating it\n");
#endif
      ioi->f_flags |= O_CREAT;
      err = 0;
    }
    else{
      fprintf(stderr,"Error: %s on %s\n",strerror(errno), ioi->filename);
      return -1;
      }
  }

  //This will overwrite existing file.TODO: Check what is the desired default behaviour 
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "Opening file %s\n", ioi->filename);
#endif
  ioi->fd = open(ioi->filename, ioi->f_flags, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
  if(ioi->fd == -1){
    fprintf(stderr,"Error: %s on %s\n",strerror(errno), ioi->filename);
    return -1;
  }
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: File opened\n");
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
  err = fallocate(ioi->fd, 0,0, prealloc_bytes);
  if(err == -1){
    fprintf(stderr, "Fallocate failed on %s", ioi->filename);
    return err;
  }
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: File preallocated\n");
#endif

  ioi->bytes_written = 0;
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Preparing iostructs\n");
#endif
  ioi->ctx =(io_context_t *) malloc(sizeof(io_context_t));
  void * errpoint = memset((void*)ioi->ctx, 0, sizeof(*(ioi->ctx)));
  if(errpoint== NULL){
    perror("Memset ctx");
    return -1;
  }
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Queue init\n");
#endif
  ret = io_queue_init(MAX_EVENTS, ioi->ctx);
  if(ret < 0){
    perror("IO_QUEUE_INIT");
    return -1;
  }
  return ret;
}

int aiow_write(struct recording_entity * re, void * start, size_t count){
  int ret;
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Performing write\n");
#endif

  struct io_info * ioi = (struct io_info * )re->opt;

  struct iocb *ib[1];
  ib[0] = (struct iocb*) malloc(sizeof(struct iocb));
  io_prep_pwrite(ib[0], ioi->fd, start, count, ioi->offset);
  io_set_callback(ib[0], wr_done);

#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Prepared write for %lu bytes\n", count);
#endif

  //Not sure if 3rd argument safe, but running 
  //one iocb at a time anyway
  ret = io_submit(*(ioi->ctx), 1, ib);

#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Submitted %d writes\n", ret);
#endif
  if(ret <0){
    perror("io_submit");
    return -1;
  }
  ioi->offset += count;
  return ret;
}
int aiow_check(struct recording_entity * re){
  //Just poll, so we can keep receiving more packets
  struct io_info * ioi = (struct io_info *)re->opt;
  static struct timespec timeout = { 0, 0 };
  struct io_event event;
  int ret = io_getevents(*(ioi->ctx), 0, 1, &event, &timeout);
  //
  if(ret > 0){
    ioi->bytes_written += event.res;
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
//TODO: Make proper sleep. io_queue_wait doesn't work
int aiow_wait_for_write(struct recording_entity* re){
  //struct rec_point * rp = (struct rec_point *) recpoint;
  struct io_info * ioi = (struct io_info *)re->opt;
  //Needs to be static so ..durr
  static struct timespec timeout = { 1, TIMEOUT_T };
  //Not sure if this works, since io_queue_run doesn't
  //work (have to use io_getevents), or then I just
  //don't know how to use it
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Buffer full. Going to sleep\n");
#endif
  //Doesn't really sleep :/
  //return io_queue_wait(*(ioi->ctx), &timeout);
  return usleep(100);
}
int aiow_close(struct recording_entity * re, void * stats){
  struct io_info * ioi = (struct io_info*)re->opt;
  close(ioi->fd);
  io_destroy(*(ioi->ctx));

  ioi->ctx = NULL;
  free(ioi->filename);

  struct stats* stat = (struct stats*)stats;
  stat->total_written += ioi->bytes_written;

  free(ioi);
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Writer closed\n");
#endif
  return 0;
}
int aiow_init_rec_entity(struct opt_s * opt, struct recording_entity * re){
  re->init = aiow_init;
  re->write = aiow_write;
  re->wait = aiow_wait_for_write;
  re->close = aiow_close;
  re->check = aiow_check;

  return re->init(opt,re);
}
