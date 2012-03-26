#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <limits.h>

#include "streamer.h"
#include "common_wrt.h"
#include "splicewriter.h"

//#define PIPE_STUFF_IN_WRITELOOP
#define IOVEC_SPLIT_TO_IOV_MAX

struct splice_ops{
  struct iovec* iov;
  int pipes[2];
};
/*
int splice_all(int from, int to, long long bytes){
  long long bytes_remaining;
  long result;

  bytes_remaining = bytes;
  while (bytes_remaining > 0) {
    result = splice(
	from, NULL,
	to, NULL,
	bytes_remaining,
	SPLICE_F_MOVE | SPLICE_F_MORE
	);

    if (result == -1)
      break;

    bytes_remaining -= result;
  }
  return result;
}
*/
int init_splice(struct opt_s *opts, struct recording_entity * re){
  int ret = common_w_init(opts, re);
  if(ret < 0)
    return -1;
  struct common_io_info * ioi = (struct common_io_info*)re->opt;
  struct splice_ops * sp = (struct splice_ops*)malloc(sizeof(struct splice_ops));
#ifndef IOVEC_SPLIT_TO_IOV_MAX
  sp->iov =(struct iovec*)malloc(sizeof(struct iovec));
#else
  sp->iov = (struct iovec*)malloc(IOV_MAX * sizeof(struct iovec));
#endif

#ifndef PIPE_STUFF_IN_WRITELOOP
  ret = pipe(sp->pipes);
  if(ret<0)
    return -1;
#endif


  ioi->extra_param = (void*)sp;

  return 1;
}

int splice_write(struct recording_entity * re, void * start, size_t count){
  int ret = 0;
  void * point_to_start=start;
  size_t trycount;
  //int err = 0;
  int i=0;
  int pagesize = sysconf(_SC_PAGE_SIZE);
  int pages_to_set = count/pagesize;
  //int nr_segs;
  unsigned long total_w =0;
#ifdef DEBUG_OUTPUT 
  fprintf(stdout, "SPLICEWRITER: Issuing write of %lu\n", count);
#endif
  struct common_io_info * ioi = (struct common_io_info*) re->opt;
  struct splice_ops *sp = (struct splice_ops *)ioi->extra_param;
#ifndef IOVEC_SPLIT_TO_IOV_MAX
  sp->iov.iov_base = start;
  sp->iov.iov_len = count;
#else
  struct iovec * iov;
#endif

  ret = count;
  /* TODO: Bidirectional
  if(ioi->read)
    ret = splice_all(ioi->fd, start, count);
  else
    ret = splice_all(start, ioi->fd, count);
    */

  while(count >0){
#ifdef IOVEC_SPLIT_TO_IOV_MAX
    point_to_start = start;
    trycount = count;
    iov  = sp->iov;
    i=0;
    /* Split the request into page aligned chunks 	*/
    /* TODO: find out where the max size is defined.	*/
    /* Currently splice can handle 16*pagesize 		*/
    while(trycount>0 && i< 16){
      iov->iov_base = point_to_start; 
      iov->iov_len = pagesize;
      point_to_start += pagesize;
      iov += 1;
      trycount-=pagesize;
      /*  Check that we don't go negative on the count */
      /* TODO: This will break if buffer entities aren't pagesize aligned */
      if(trycount>=0)
      i++;
    }
#ifdef DEBUG_OUTPUT
    fprintf(stdout, "Prepared iovec of %i elements for %i bytes\n", i, i*pagesize);
#endif
#endif
#ifdef PIPE_STUFF_IN_WRITELOOP
      err = pipe(sp->pipes);
      if(err<0)
	break;
#endif
#ifdef IOVEC_SPLIT_TO_IOV_MAX
      ret = vmsplice(sp->pipes[1], sp->iov, i, SPLICE_F_GIFT);
#else
      ret = vmsplice(sp->pipes[1], sp->iov, 1, SPLICE_F_GIFT);
#endif
      if(ret <0)
	break;
#ifdef DEBUG_OUTPUT 
#ifdef IOVEC_SPLIT_TO_IOV_MAX
      else
	fprintf(stdout, "Vmsplice accepted %i bytes when given %i bytes\n", ret, i*pagesize);
#endif
      start += ret;
      count -= ret;
#endif

#ifndef IOVEC_SPLIT_TO_IOV_MAX
      count -= ret;
#endif
      ret = splice(sp->pipes[0], NULL, ioi->fd, NULL, i*pagesize, SPLICE_F_MOVE);
      if(ret<0)
	break;
#ifndef IOVEC_SPLIT_TO_IOV_MAX
      sp->iov->iov_base += ret;
      sp->iov->iov_len = count;
#endif

      // Update statistics 
      ioi->bytes_exchanged += ret;
      total_w += ret;
#ifdef PIPE_STUFF_IN_WRITELOOP
      close(sp->pipes[0]);
      close(sp->pipes[1]);
#endif
  }
  if(ret <0){
    perror("SPLICEWRITER: Error on write/read");
    fprintf(stderr, "SPLICEWRITER: Error happened on %s with start %lu and count %lu\n", ioi->filename, (long)start, count);
    return 0;
  }
  else{
#ifdef DEBUG_OUTPUT 
  fprintf(stdout, "SPLICEWRITER: Write done for %lu in %d loops\n", total_w, i);
#endif
  }

  return total_w;
}
int splice_get_w_fflags(){
  return O_WRONLY|O_DIRECT|O_NOATIME;
  }
int splice_get_r_fflags(){
  return O_RDONLY|O_DIRECT|O_NOATIME;
  }
int splice_close(struct recording_entity *re, void *stats){
  struct common_io_info * ioi = (struct common_io_info*)re->opt;
  struct splice_ops * sp = (struct splice_ops*)ioi->extra_param;
  close(sp->pipes[0]);
  close(sp->pipes[1]);
  free(sp->iov);
  free(ioi->extra_param);
  return common_close(re,stats);
}

int splice_init_splice(struct opt_s *opt, struct recording_entity *re){
  re->init = init_splice;
  re->close = splice_close;
  re->write_index_data = common_write_index_data;

  re->write = splice_write;
  
  re->get_n_packets = common_nofpacks;
  re->get_packet_index = common_pindex;

  re->get_filename = common_wrt_get_filename;

  re->get_r_flags = splice_get_r_fflags;
  re->get_w_flags = splice_get_w_fflags;

  return re->init(opt,re);
}
