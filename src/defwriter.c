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
  struct common_io_info * ioi = (struct common_io_info*) re->opt;
  if(ioi->fd == 0){
    E("FD not set! Not writing to stdout!");
    return -1;
    }

  /* Loop until we've gotten everything written */
  while(count >0){
#if(DEBUG_OUTPUT) 
    fprintf(stdout, "DEFWRITER: Issuing write of %lu with start %lu to %s\n", count,(long unsigned)start, ioi->curfilename);
#endif
    if(ioi->optbits & READMODE)
      ret = read(ioi->fd, start, count);
    else
      ret = write(ioi->fd, start, count);
    if(ret <=0){
      if(ret == 0 && (ioi->optbits & READMODE)){
#if(DEBUG_OUTPUT)
	fprintf(stdout, "DEFWRITER: End of file!\n");
#endif
	total_w += count;
	ioi->bytes_exchanged += count;
	return count;
      }
      else{
	perror("DEFWRITER: Error on write/read");
	fprintf(stderr, "DEFWRITER: Error happened on %s with count %lu, error: %ld\n", ioi->filename,  count, ret);
	return -1;
      }
    }
    else{
#if(DEBUG_OUTPUT) 
      fprintf(stdout, "DEFWRITER: Write done for %ld\n", ret);
#endif
      if((unsigned long)ret < count)
	fprintf(stderr, "DEFWRITER: Write wrote only %ld out of %lu\n", ret, count);
      total_w += ret;
      count -= ret;
      ioi->bytes_exchanged += ret;
    }
  }
  return total_w;
}
int def_get_w_fflags(){
  //return O_WRONLY|O_DIRECT|O_NOATIME;
  return O_WRONLY|O_NOATIME;
}
int def_get_r_fflags(){
  return O_RDONLY|O_DIRECT|O_NOATIME;
}

int def_init_def(struct opt_s *opt, struct recording_entity *re){
  common_init_common_functions(opt,re);
  //re->init = common_w_init;
  //re->close = common_close;
  //re->write_index_data = common_write_index_data;

  re->write = def_write;
  
  //re->get_n_packets = common_nofpacks;
  //re->get_packet_index = common_pindex;

  //re->get_filename = common_wrt_get_filename;
  //re->getfd = common_getfd;

  re->get_r_flags = def_get_r_fflags;
  re->get_w_flags = def_get_w_fflags;

  return re->init(opt,re);
}
int rec_init_dummy(struct opt_s *op , struct recording_entity *re){
  /*
  re->init = null;
  re-write = dummy_write_wrapped;
  */
  return 0;
}

