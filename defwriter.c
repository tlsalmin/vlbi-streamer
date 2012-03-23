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
  struct common_io_info * ioi = (struct common_io_info*) ioi;
  if(ioi->read)
    ret = read(ioi->fd, start, count);
  else
    ret = write(ioi->fd, start, count);

  
  //ret = write(

  return ret;
}

int def_init_def(struct opt_s *opt, struct recording_entity *re){
  re->init = common_w_init;
  //writeloop
  re->close = common_close;
  re->write_index_data = common_write_index_data;
  
  re->get_n_packets = common_nofpacks;
  re->get_packet_index = common_pindex;

  return re->init(opt,re);
}
