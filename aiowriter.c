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
#define MAX_IOCB 2
#define IOVEC_MAX 2

//TODO: Error handling
struct io_info{
  io_context_t ctx;
  struct iocb* iocbpp[MAX_IOCB];
  //struct io_event events[10];
  struct rec_point * rp;
};

/* Fatal error handler */
static void io_error(const char *func, int rc)
{
  /*
  if (rc < 0 && -rc < sys_nerr)
    fprintf(stderr, "%s: %s", func, sys_errlist[-rc]);
  else
  */
    fprintf(stderr, "%s: error %d", func, rc);
}

static void wr_done(io_context_t ctx, struct iocb *iocb, long res, long res2){
  if(res2 != 0)
    io_error("aio write", res2);
  if(res != iocb->u.c.nbytes){
    fprintf(stderr, "write missed bytes expect %lu got %li", iocb->u.c.nbytes, res2);
  }
  free(iocb);
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "Write callback done\n");
#endif
}

int aiow_init(void * ringbuf, void * recpoint){
  struct ringbuf * rbuf = (struct ringbuf *)ringbuf;
  struct rec_point * rp = (struct rec_point *)recpoint;
  int ret;
  void * errpoint;
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Memaligning buffer\n");
#endif
  ret = posix_memalign((void**)&(rbuf->buffer), sysconf(_SC_PAGESIZE), rbuf->num_elems*rbuf->elem_size);
  if (ret < 0 || rbuf->buffer == 0) {
    perror("make_write_buffer");
    return -1;
  }
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Preparing iostructs\n");
#endif
  //TODO: Written on friday. Check if easier way
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
  //NOTE: Don't have to initialize iocb further
  //allocated and used in write
  //ioi->iocbpp = (struct iocb *) malloc(sizeof(struct iocb));
  //Copy file descriptor
  //ioi->iocbpp->aio_fildes = rp->fd;
  //Use this in write
  //io_prep_write(ioi->iocbpp, rp->fd, rbuf->buffer, rbuf->num_elems*rbuf->elem_size, 0);
  return 0;
}
//TODO
int aiow_write(void * ringbuf, void * recpoint){
  int ret;
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Performing write\n");
#endif
  struct ringbuf * rbuf = (struct ringbuf *)ringbuf;
  struct rec_point * rp = (struct rec_point *)recpoint;
  struct io_info * ioi = (struct io_info * )rp->iostruct;
  int vecs;
  //rbuf->ready_to_write = 0;
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Blocking writes\n");
#endif

  ioi->iocbpp[0] = (struct iocb *) malloc(sizeof(struct iocb));
  if(ioi->iocbpp[0] == NULL){
    perror("malloc iocbpp");
    return -1;
  }
  struct iovec iov[IOVEC_MAX];
  gen_iov(rbuf, &vecs, &iov);
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Generated write vectors\n");
#endif

  io_prep_pwritev(ioi->iocbpp[0], rp->fd, iov, vecs, rp->offset);
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Prepared write\n");
#endif

  io_set_callback(ioi->iocbpp[0], wr_done);

  //Not sure if 3rd argument safe, but running 
  //one iocb at a time anyway
  ret = io_submit(ioi->ctx, 1, ioi->iocbpp);
#ifdef DEBUG_OUTPUT
  if(ret > 0)
    fprintf(stdout, "AIOW: Submitted %d writes\n", ret);
  else{
    perror("io_submit");
    return -1;
  }
#endif
  //free(iov);
  return 0;
}
int aiow_check(void * recpoint){
  int ret;
  struct rec_point * rp = (struct rec_point *)recpoint;
  struct io_info * ioi = (struct io_info *)rp->iostruct;
  ret = io_queue_run(ioi->ctx);
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Check return %d\n", ret);
#endif
  /*
   * Just using one iocb
   * TODO: Change implementation for reads also
   * Might need a unified writer backend to
   * queue reads properly, thought reads
   * go to different file
   */
  //Done in callback
  //if(ret > 0)
  //free(ioi->iocbpp[0]);
  return ret;
}
//Not used, since can't update status etc.
//Using queue-stuff instead
int aiow_wait_for_write(void * recpoint, double timeout){
  struct rec_point * rp = (struct rec_point *) recpoint;
  struct io_info * ioi = (struct io_info *)rp->iostruct;
  //TODO: Implement timeout
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Buffer full. Going to sleep\n");
#endif
  return io_queue_wait(ioi->ctx, NULL);
}
void aiow_write_done(){
}
int aiow_close(void * ioinfo, void* ringbuf){
  struct io_info * ioi = (struct io_info*) ioinfo;
  struct ringbuf * rbuf = (struct ringbuf *)ringbuf;
  io_destroy(ioi->ctx);
  //free(ioi->iocbpp);
  free(rbuf->buffer);
  free(ioi);
  return 0;
}
