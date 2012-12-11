#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/mman.h>
#include <errno.h>
#include <limits.h>

#include "streamer.h"
#include "confighelper.h"
#include "config.h"
#include "common_filehandling.h"
#include "disk2file.h"
#include "common_wrt.h"

#define D2FEXIT LOG("D2f exiting!\n"); free(iov);do{if(se->be != NULL){ set_free(opt->membranch, se->be->self); se->be = NULL; }}while(0); return NULL

extern FILE* logtofile;

int d2f_init_disk2file(struct opt_s * opt, struct streamer_entity *se)
{
  struct d2fopts_s* d2fopt;
  //struct stat statinfo;
  //(void)opt;
  //(void)se;
  se->opt = (void*)malloc(sizeof(struct d2fopts_s));
  d2fopt = (struct d2fopts_s*)se->opt;
  d2fopt->opt = opt;
  d2fopt->missing = 0;
  d2fopt->written = 0;

  return common_open_file(&(d2fopt->fd), O_WRONLY, opt->disk2fileoutput, 0);


  /*
  d2fopt->fd = open(opt->disk2fileoutput, O_WRONLY|O_CREAT);
  if(d2fopt->fd < 0){
    E("Error in d2f file open");
    perror("D2F file open");
    return -1;
  }
  */

  return 0;
}
void d2f_get_stats(void* daopt, void* statsiguess){
  struct stats *stat = (struct stats * ) statsiguess;
  struct d2fopts_s *d2fopt = (struct d2fopts_s*)daopt;
  //if(spec_ops->opt->optbits & USE_RX_RING)
  //stat->total_packets += *spec_ops->opt->total_packets;
  stat->total_bytes += d2fopt->written;
  //stat->incomplete += spec_ops->incomplete;
  stat->dropped += d2fopt->missing;
}
void d2f_close_file(struct streamer_entity *se){
  struct d2fopts_s *d2fopt = (struct d2fopts_s*)se->opt;
  int err = close(d2fopt->fd);
  if(err <0){
    E("Error closing fd");
    perror("close d2f fd");
  }
}
int d2f_close(void * daopt, void* statsiguess){
  struct d2fopts_s* d2fopt = (struct d2fopts_s*) daopt;
  d2f_get_stats(daopt,statsiguess);
  int err = close(d2fopt->fd);
  if(err <0){
    E("Error closing fd");
    perror("close d2f fd");
  }

  //(void)daopt;
  //(void)statsiguess;
  return 0;
}
//int main(int argc, char **argv) {
void* disk2file(void * streamo)
{
  struct streamer_entity *se =(struct streamer_entity*)streamo;
  struct d2fopts_s* d2fopt = se->opt;
  struct opt_s * opt = d2fopt->opt;
  struct iovec *iov = (struct iovec*)malloc(sizeof(struct iovec)*IOV_MAX);
  int err =0;
  //struct opt_s * writeopt;
  void* buf;
  long* inc;
  unsigned int i, total_i;
  if(opt->offset != 0)
    LOG("Starting disk2file procedure. Stripping %i bytes from header\n", opt->offset);
  else
    LOG("Starting disk2file procedure\n");

  struct sender_tracking st;
  init_sender_tracking(opt, &st);

  throttling_count(opt, &st);

  //loadup_n(opt, &st);

  opt->status = STATUS_RUNNING;

  //st.packetpeek = *(opt->total_packets);

  err = jump_to_next_file(opt, se, &st);

  //TODO: Better err handling 
  if(se->be == NULL){
    E("Coulnt get buffer so exiting");
    D2FEXIT;
  }
  buf = se->be->simple_get_writebuf(se->be, &inc);
  total_i=0;


  LOG("D2F running\n");

  long packetpeek = get_n_packets(opt->fi);

  while(should_i_be_running(opt, &st) == 1){
    if(total_i == (unsigned int)opt->buf_num_elems || (st.packets_sent - packetpeek == 0)){
      err = jump_to_next_file(opt, se, &st);
      packetpeek = get_n_packets(opt->fi);
      if(err == ALL_DONE){
	D2FEXIT;
      }
      else if (err < 0){
	E("Error in getting buffer");
	D2FEXIT;
      }
      buf = se->be->simple_get_writebuf(se->be, &inc);
      total_i=0;
      //i=0;
    }
    unsigned int real_num_packets = MIN((unsigned int)opt->buf_num_elems, (unsigned int)packetpeek);
    while(total_i < real_num_packets)
    {
      for(i=0;i<MIN(real_num_packets-total_i,IOV_MAX);i++){
	iov[i].iov_base = buf + ((long)i)*opt->packet_size + ((long)opt->offset);
	iov[i].iov_len = opt->packet_size - opt->offset;
      }
      err = (long)writev(d2fopt->fd, iov, i);
      if(err < 0){
	perror("D2f: Error on write");
	E("Tried to write %d vecs ",, i);
	D2FEXIT;
      }
      else{
	st.packets_sent += i;
	d2fopt->written += i*opt->packet_size - i*opt->offset;
	buf += i*opt->packet_size;
	total_i += i;
      }
    }
  }


  D2FEXIT;
}
void disk2filestop(struct streamer_entity *se){
  D("Stopping loop");
  ((struct opt_s*)((struct d2fopts_s *)se->opt)->opt)->status = STATUS_STOPPED;

}
void d2f_init_default_functions(struct opt_s *opt, struct streamer_entity *se)
{
  (void)opt;
  se->init = d2f_init_disk2file;
  se->close = d2f_close;
  se->get_stats = d2f_get_stats;
  se->close_socket = d2f_close_file;
  se->start = disk2file;
  se->stop = disk2filestop;
  //se->is_running = udps_is_running;
  //se->get_max_packets = udps_get_max_packets;
}
int d2f_init( struct opt_s *opt, struct streamer_entity *se)
{

  d2f_init_default_functions(opt,se);
  /*
     if(opt->optbits & USE_RX_RING)
     se->start = udp_rxring;
     else
     se->start = udp_receiver;
     se->stop = udps_stop;
     */

  return se->init(opt, se);
}
