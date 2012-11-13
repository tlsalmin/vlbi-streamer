#include "streamer.h"
#include "common_wrt.h"

struct extra_parameters{
  struct iovec * iov;
};

extern FILE* logfile;

int writev_init(struct opt_s * opt, struct recording_e *re){
  int ret;
  struct extra_parameters * ep;

  ret = common_w_init(opt,re);
  if(ret!=0){
    fprintf(stderr, "Common w init returned error %d\n", ret);
    return ret;
  }

  struct common_io_info * ioi = (struct common_io_info *) re->opt;

  D("Preparing iovecs");
  //ib[0] = (struct iocb*) malloc(sizeof(struct iocb));
  ioi->extra_param = (void*) malloc(sizeof(struct extra_parameters));
  CHECK_ERR_NONNULL(ioi->extra_param, "Malloc extra params");
  ep = (struct extra_parameters *) ioi->extra_param;

  ioi->extra_param->iov = NULL;
  ioi->extra_param->iov = (struct iovec*)malloc(sizeof(struct iovec)*opt->buf_num_elems);
  CHECK_ERR_NONNULL(ioi->extra_param->iov, "Malloc iov");
  
  return 0;
}
int writev_get_w_fflags(){
    return  O_WRONLY|O_NOATIME;
    //return  O_WRONLY|O_NOATIME|O_NONBLOCK;
    //return  O_WRONLY|O_DIRECT|O_NOATIME;
}
int writev_get_r_fflags(){
    return  O_RDONLY|O_NOATIME;
    //return  O_RDONLY|O_DIRECT|O_NOATIME;
}
long aiow_write(struct recording_entity * re, void * start, size_t count){
  int n_vecs;
  struct common_io_info * ioi = (struct common_io_info * )re->opt;
  struct extra_parameters * ep = (struct extra_parameters*)ioi->extra_param;

  n_vecs = 
}
int aiow_close(struct recording_entity * re, void * stats){
  struct common_io_info * ioi = (struct common_io_info*)re->opt;
  struct extra_parameters *ep = (struct extra_parameters*)ioi->extra_param;

  free(ep->iov);
  free(ep);
  common_close(re,stats);

  return 0;

}
int writev_init_rec_entity(struct opt_s * opt, struct recording_entity * re){

  common_init_common_functions(opt,re);
  re->init = writev_init;
  re->write = writev_write;
  re->close = writev_close;
  re->get_r_flags = writev_get_r_fflags;
  re->get_w_flags = writev_get_w_fflags;

  return re->init(opt,re);
}
