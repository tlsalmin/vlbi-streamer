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
#include "defwriter.h"
#include "common_wrt.h"
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

int splice_write(struct recording_entity * re, void * start, size_t count){
  int ret = 0;
#ifdef DEBUG_OUTPUT 
  fprintf(stdout, "DEFWRITER: Issuing write of %lu\n", count);
#endif
  struct common_io_info * ioi = (struct common_io_info*) re->opt;
  struct iovec iov;
  iov.iov_base = start;
  iov.iov_len = count;
  int pipes[2];
  ret = pipe(pipes);
  if (ret<0)
    return -1;

  ret = count;
  /*
  if(ioi->read)
    ret = splice_all(ioi->fd, start, count);
  else
    ret = splice_all(start, ioi->fd, count);
    */
  while(count >0){
    //vmsplice(pipes[1], &iov, 
    /* 
     * TODO: Friday dev ended here!
     */

  }
  if(ret <0){
    perror("DEFWRITER: Error on write/read");
    fprintf(stderr, "DEFWRITER: Error happened on %s with start %lu and count %lu\n", ioi->filename, (long)start, count);
    return 0;
  }
  else{
#ifdef DEBUG_OUTPUT 
  fprintf(stdout, "DEFWRITER: Write done for %d\n", ret);
#endif
  ioi->bytes_exchanged += ret;
  }

  return ret;
}
int splice_get_w_fflags(){
  return O_WRONLY|O_DIRECT|O_NOATIME;
  }
int splice_get_r_fflags(){
  return O_RDONLY|O_DIRECT|O_NOATIME;
  }

int splice_init_def(struct opt_s *opt, struct recording_entity *re){
  re->init = common_w_init;
  re->close = common_close;
  re->write_index_data = common_write_index_data;

  re->write = splice_write;
  
  re->get_n_packets = common_nofpacks;
  re->get_packet_index = common_pindex;

  re->get_filename = common_wrt_get_filename;

  re->get_r_flags = splice_get_r_fflags;
  re->get_w_flags = splice_get_w_fflags;

  return re->init(opt,re);
}
