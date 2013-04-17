#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>
/* FOR IOV_MAX */
#include <limits.h>
#include <sys/mman.h>

#include "streamer.h"
#include "common_wrt.h"
#include "defwriter.h"
#include "writev_writer.h"


extern FILE* logfile;

int writev_init(struct opt_s * opt, struct recording_entity *re){
  int ret;
  //struct extra_parameters * ep;

  ret = common_w_init(opt,re);
  if(ret!=0){
    fprintf(stderr, "Common w init returned error %d\n", ret);
    return ret;
  }

  struct common_io_info * ioi = (struct common_io_info *) re->opt;

  D("Preparing iovecs. IOV_MAX is %d",, IOV_MAX);
  //ib[0] = (struct iocb*) malloc(sizeof(struct iocb));
  ioi->extra_param = (void*)malloc(sizeof(struct iovec)*IOV_MAX); 
  CHECK_ERR_NONNULL(ioi->extra_param, "Malloc extra params");
  //ep = (struct extra_parameters *) ioi->extra_param;

  
  return 0;
}
int writev_get_w_fflags(){
    return  O_WRONLY|O_NOATIME|O_SYNC;
    //return  O_WRONLY|O_NOATIME|O_NONBLOCK;
    //return  O_WRONLY|O_DIRECT|O_NOATIME;
}
int writev_get_r_fflags(){
    return  O_RDONLY|O_NOATIME|O_SYNC;
    //return  O_RDONLY|O_DIRECT|O_NOATIME;
}
long writev_write(struct recording_entity * re, void * start, size_t count){
  /* Just make me long.. */
  long n_vecs, i, total_i;
  long err;
  struct common_io_info * ioi = (struct common_io_info * )re->opt;
  struct iovec * iov = (struct iovec*)ioi->extra_param;
  off_t orig_offset = ioi->offset;
  /* No point in doing a scatter read */
  if(ioi->opt->optbits & READMODE)
    return def_write(re,start,count);

  D("Issued write of %lu from %lu on %s. offset is %d file offset %ld",, count, (long)start, ioi->curfilename, ioi->opt->offset, ioi->offset);
  total_i=0;
  n_vecs = count/ioi->opt->packet_size;
  while(total_i < n_vecs){
    for(i=0;i<MIN(IOV_MAX, n_vecs-total_i);i++){
      iov[i].iov_base = start + (i+total_i)*ioi->opt->packet_size + ioi->opt->offset;
      iov[i].iov_len = ioi->opt->packet_size - ioi->opt->offset;
    }
    err = (long)pwritev(ioi->fd, iov, i, ioi->offset);
    if(err < 0){
      perror("WRITEV: Error on write");
      E("Tried to write %ld vecs for %ld bytes",, i, count);
      return -1;
    }
    else if((unsigned long)err !=  i*(ioi->opt->packet_size - ioi->opt->offset))
      E("Wrote %ld when should have %ld",, err, (i * (ioi->opt->packet_size - ioi->opt->offset)));
    //start += i*ioi->opt->packet_size;
    ioi->offset += i*(ioi->opt->packet_size - ioi->opt->offset);
    total_i += i;
  }
#if(DAEMON)
  //if (pthread_spin_lock((ioi->opt->augmentlock)) != 0)
    //E("spinlock lock");
  ioi->opt->bytes_exchanged += total_i*(ioi->opt->packet_size- ioi->opt->offset);

  //if (pthread_spin_unlock((ioi->opt->augmentlock)) != 0)
    //E("Spinlock unlock");
#endif
  ioi->bytes_exchanged += total_i*(ioi->opt->packet_size - ioi->opt->offset);

  D("Writev wrote %lu for %s",, total_i*(ioi->opt->packet_size- ioi->opt->offset), ioi->curfilename);
  fdatasync(ioi->fd);
  if(posix_fadvise(ioi->fd, orig_offset, count, POSIX_FADV_DONTNEED)!= 0)
    E("Error in posix_fadvise");
  if(posix_madvise(start, count, POSIX_MADV_DONTNEED) != 0)
    E("Error in posix_madvise");


  /* Returning count since simplebuffer sdhouln't think about these things */
  return count;
}

int writev_close(struct recording_entity * re, void * stats){
  struct common_io_info * ioi = (struct common_io_info*)re->opt;
  struct extra_parameters *ep = (struct extra_parameters*)ioi->extra_param;

  free(((struct iovec*)ep));
  common_close(re,stats);

  return 0;

}
int writev_init_rec_entity(struct opt_s * opt, struct recording_entity * re){

  D("Initializing a writev recpoint");
  common_init_common_functions(opt,re);
  re->init = writev_init;
  re->write = writev_write;
  re->close = writev_close;
  re->get_r_flags = writev_get_r_fflags;
  re->get_w_flags = writev_get_w_fflags;

  return re->init(opt,re);
}
