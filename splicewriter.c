#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#include "streamer.h"
#include "common_wrt.h"
#include "splicewriter.h"
struct splice_ops{
  struct iovec iov;
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

  ret = pipe(sp->pipes);
  if(ret<0)
    return -1;


  ioi->extra_param = (void*)sp;

  return 1;
}

int splice_write(struct recording_entity * re, void * start, size_t count){
  int ret = 0;
#ifdef DEBUG_OUTPUT 
  fprintf(stdout, "SPLICEWRITER: Issuing write of %lu\n", count);
#endif
  struct common_io_info * ioi = (struct common_io_info*) re->opt;
  struct splice_ops *sp = (struct splice_ops *)ioi->extra_param;
  sp->iov.iov_base = start;
  sp->iov.iov_len = count;

  ret = count;
  /*
  if(ioi->read)
    ret = splice_all(ioi->fd, start, count);
  else
    ret = splice_all(start, ioi->fd, count);
    */
  while(count >0){
    ret = vmsplice(sp->pipes[1], &(sp->iov), 1, SPLICE_F_GIFT);
    if(ret <0)
      break;
    count -= ret;
    ret = splice(sp->pipes[0], NULL, ioi->fd, NULL, ret, SPLICE_F_MOVE);
    if(ret<0)
      break;
    sp->iov.iov_base += ret;
    sp->iov.iov_len = count;
    
    /* Update statistics */
    ioi->bytes_exchanged += ret;
    /* 
     * TODO: Friday dev ended here!
     */
  }
  if(ret <0){
    fprintf(stderr, "HURRRR\n");
    perror("SPLICEWRITER: Error on write/read");
    fprintf(stderr, "SPLICEWRITER: Error happened on %s with start %lu and count %lu\n", ioi->filename, (long)start, count);
    return 0;
  }
  else{
#ifdef DEBUG_OUTPUT 
  fprintf(stdout, "SPLICEWRITER: Write done for %d\n", ret);
#endif
  }

  return ret;
}
int splice_get_w_fflags(){
  return O_WRONLY|O_DIRECT|O_NOATIME;
  }
int splice_get_r_fflags(){
  return O_RDONLY|O_DIRECT|O_NOATIME;
  }
int splice_close(struct recording_entity *re, void *stats){
  struct common_io_info * ioi = (struct common_io_info*)re->opt;
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
