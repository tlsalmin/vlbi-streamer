#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include "config.h"

#include "streamer.h"
#include "defwriter.h"
#include "common_wrt.h"

long def_write(struct recording_entity * re, void * start, size_t count){
  long ret = 0;
  long total_w = 0;
#ifdef DEBUG_OUTPUT 
  fprintf(stdout, "DEFWRITER: Issuing write of %lu\n", count);
#endif
  struct common_io_info * ioi = (struct common_io_info*) re->opt;

  /* Loop until we've gotten everything written */
  while(count >0){
    if(ioi->optbits & READMODE)
      ret = read(ioi->fd, start, count);
    else
      ret = write(ioi->fd, start, count);
    if(ret <=0){
      perror("DEFWRITER: Error on write/read");
      fprintf(stderr, "DEFWRITER: Error happened on %s with count %lu\n", ioi->filename,  count);
      return -1;
    }
    else{
#ifdef DEBUG_OUTPUT 
      fprintf(stdout, "DEFWRITER: Write done for %ld\n", ret);
#endif
      if(ret < count)
	fprintf(stderr, "DEFWRITER: Write wrote only %ld out of %lu", ret, count);
      total_w += ret;
      count -= ret;
      ioi->bytes_exchanged += ret;
    }
  }
  return total_w;
}
int def_get_w_fflags(){
  return O_WRONLY|O_DIRECT|O_NOATIME;
}
int def_get_r_fflags(){
  return O_RDONLY|O_DIRECT|O_NOATIME;
}

int def_init_def(struct opt_s *opt, struct recording_entity *re){
  re->init = common_w_init;
  re->close = common_close;
  re->write_index_data = common_write_index_data;

  re->write = def_write;
  
  re->get_n_packets = common_nofpacks;
  re->get_packet_index = common_pindex;

  re->get_filename = common_wrt_get_filename;
  re->getfd = common_getfd;

  re->get_r_flags = def_get_r_fflags;
  re->get_w_flags = def_get_w_fflags;

  return re->init(opt,re);
}
int rec_init_dummy(struct opt_s *op , struct recording_entity *re){
  /*
  re->init = null;
  re-write = dummy_write_wrapped;
  */
  return 1;
}

