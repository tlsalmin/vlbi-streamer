#include "dummywriter.h"
#include "common_wrt.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
int dummy_get_w_fflags(){
  return 0;
}
int dummy_get_r_fflags(){
  return 0;
}
long dummy_write(struct recording_entity * re, void* s, size_t count){
  (void)re;
  (void)s;
  return count;
}
int dummy_acquire(void* recco, void* opti, void * acq)
{
  //(void*)opti;
  struct recording_entity * re = (struct recording_entity*)recco;
  struct common_io_info * ioi = (struct common_io_info*)re->opt;
  ioi->opt = (struct opt_s*)opti;

  ioi->file_seqnum = *((unsigned long*)acq);

  sprintf(ioi->curfilename, "%s%i%s%s%s%s.%08ld", ROOTDIRS, ioi->id, "/",ioi->opt->filename, "/",ioi->opt->filename,ioi->file_seqnum); 

  D("Opening file(not really!) %s",,ioi->curfilename);

  ioi->filesize = ioi->opt->buf_num_elems *  ioi->opt->packet_size;

  return 0;
}
int dummy_release(void* recco)
{
  struct recording_entity * re = (struct recording_entity*)recco;
  struct common_io_info * ioi = (struct common_io_info*)re->opt;
  memset(ioi->curfilename, 0, sizeof(char)*FILENAME_MAX);
  ioi->opt = NULL;
  return 0;
}
int dummy_init_dummy(struct opt_s * opt, struct recording_entity *re){
  int err;
  common_init_common_functions(opt,re);

  re->write = dummy_write;

  re->get_r_flags = dummy_get_r_fflags;
  re->get_w_flags = dummy_get_w_fflags;

  D("Running default init");

  err = re->init(opt,re);
  if(err != 0){
    E("Err in init");
  }

  re->self->acquire = dummy_acquire;
  re->self->release = dummy_release;

  return 0;
}
