#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h> /* For madvise */
#include <sys/sendfile.h> /* For madvise */
#include <sys/fcntl.h>
//#define _ASM_GENERIC_FCNTL_H
//#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/mman.h> /* For madvise */
//#include <fcntl.h>

#include "config.h"

#include "streamer.h"
#include "common_wrt.h"
#include "sendfile_writer.h"

long sendfile_write(struct recording_entity * re, void *start , size_t count)
{
  long tosend = count;
  long ret, total_w=0;
  struct common_io_info * ioi = (struct common_io_info*) re->opt;
  off_t offset = start - ioi->bufstart;
  D("SENDFILE_WRITER: Issuing write of %lu to %s",, count, ioi->curfilename);

  while(tosend >0)
  {
    if(ioi->opt->optbits & READMODE)
      ret = sendfile(ioi->shmid, ioi->fd, &(ioi->offset),tosend);
    else
      ret = sendfile(ioi->fd, ioi->shmid, &offset, tosend);
    if(ret < 0){
      E("Sendfile borked");
      break;
    }
    tosend -=ret;
    //ioi->offset +=ret;
    total_w+=ret;
  }
  if(ret < 0 )
    return -1;
  ioi->bytes_exchanged += total_w;
  ioi->opt->bytes_exchanged += total_w;

  return total_w;
}
int sfwrite_get_w_fflags(){
  //return O_WRONLY|O_DIRECT|O_NOATIME;
  return O_WRONLY|O_NOATIME;
}
int sfwrite_get_r_fflags(){
  //return O_RDONLY|O_DIRECT|O_NOATIME;
  return O_RDONLY|O_NOATIME;
}
int init_sendfile_writer(struct opt_s *opt, struct recording_entity *re){

  common_init_common_functions(opt,re);
  re->init = common_w_init;
  re->close = common_close;
  re->write = sendfile_write;
  re->get_r_flags = sfwrite_get_r_fflags;
  re->get_w_flags = sfwrite_get_w_fflags;

  return re->init(opt,re);
}
