#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#include "streamer.h"
#include "defwriter.h"
#include "common_wrt.h"

int def_write(struct recording_entity * re, void * start, size_t count){
  int ret = 0;
#ifdef DEBUG_OUTPUT 
  fprintf(stdout, "DEFWRITER: Issuing write of %lu\n", count);
#endif
  struct common_io_info * ioi = (struct common_io_info*) re->opt;
  if(ioi->read)
    ret = read(ioi->fd, start, count);
  else
    ret = write(ioi->fd, start, count);
#ifdef DEBUG_OUTPUT 
  fprintf(stdout, "DEFWRITER: Write done for %d\n", ret);
#endif
  ioi->bytes_exchanged += ret;

  return ret;
}

int def_init_def(struct opt_s *opt, struct recording_entity *re){
  re->init = common_w_init;
  re->close = common_close;
  re->write_index_data = common_write_index_data;

  re->write = def_write;
  
  re->get_n_packets = common_nofpacks;
  re->get_packet_index = common_pindex;
  re->get_filename = common_wrt_get_filename;

  return re->init(opt,re);
}
