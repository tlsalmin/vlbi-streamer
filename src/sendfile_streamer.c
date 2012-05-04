
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <time.h>
#ifdef MMAP_TECH
#include <sys/mman.h>
#include <sys/poll.h>
#endif

#include <pthread.h>

#include <unistd.h>

#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <arpa/inet.h>
#ifdef HAVE_LINUX_NET_TSTAMP_H
#include <linux/net_tstamp.h>
#endif
#include <netinet/in.h>

#include <net/if.h>
#include "config.h"
#include "streamer.h"
#include "udp_stream.h"
//Moved to generic, shouldn't need anymore
//#include "ringbuf.h"
//#include "aiowriter.h"

void* sendfile_writer(void *se)
{
  int err = 0;
  int i=0;
  struct streamer_entity *be =(struct streamer_entity*) se;
  struct opts *spec_ops = (struct opts *)be->opt;
  //time_t t_start;
  //double time_left=0;
  INDEX_FILE_TYPE *daspot = spec_ops->packet_index;
  spec_ops->total_captured_bytes = 0;
  spec_ops->total_captured_packets = 0;
#ifdef CHECK_OUT_OF_ORDER
  spec_ops->out_of_order = 0;
#endif
  spec_ops->incomplete = 0;
  spec_ops->dropped = 0;

  if (spec_ops->fd < 0)
    exit(spec_ops->fd);
  int ffd = spec_ops->be->recer->getfd(spec_ops->be->recer);

  int pipes[2];
  err = pipe(pipes);
  if(err < 0){
    perror("pipes");
    pthread_exit(NULL);
  }
  fprintf(stdout, "WUT\n");
  //listen(spec_ops->fd, 2);

  //time(&t_start);

#ifdef DEBUG_OUTPUT
  fprintf(stdout, "UDP_STREAMER: Starting stream capture\n");
#endif
  //while((time_left = ((double)spec_ops->time-difftime(time(NULL), t_start))) > 0){
  while(spec_ops->running){
    //void * buf;
    //long unsigned int nth_package;
    /* This breaks separation of modules. Needs to be implemented in every receiver */

      //Try a semaphore here to limit interrupt utilization.
      //Probably doesn't help .. Actually worked really nicely to 
      //reduce Software interrupts on one core!
      //TODO: read doesn't timeout if we aren't receiving any packets

      //Critical sec in logging n:th packet
      pthread_mutex_lock(spec_ops->cumlock);
      err = splice(spec_ops->fd, 0, pipes[1], 0, spec_ops->buf_elem_size,SPLICE_F_MOVE|SPLICE_F_MORE);
      //err = splice(spec_ops->fd, 0, pipes[1], 0, 500,0);

      if(err < 0){
	if(err == EINTR)
	  fprintf(stdout, "UDP_STREAMER: Main thread has shutdown socket\n");
	else
	  perror("RECV error");
	pthread_mutex_unlock(spec_ops->cumlock);
	break;
      }
      else{
	splice(pipes[0], NULL, ffd, NULL, spec_ops->buf_elem_size, SPLICE_F_MOVE|SPLICE_F_MORE);
      }
      pthread_mutex_unlock(spec_ops->cumlock);
      /*
      else{
	*daspot = *(spec_ops->cumul);
	*(spec_ops->cumul) += 1;
	pthread_mutex_unlock(spec_ops->cumlock);
      }


#ifdef CHECK_OUT_OF_ORDER
      spec_ops->last_packet = *daspot;
#endif

      */
      spec_ops->total_captured_bytes +=(unsigned int) err;
      spec_ops->total_captured_packets += 1;
      if(spec_ops->total_captured_packets < spec_ops->max_num_packets)
	daspot++;
      else{
	fprintf(stderr, "UDP_STREAMER: Out of space on index file");
	break;
      }
    //If write buffer is full

  /*
    if(spec_ops->handle_packet != NULL)
      spec_ops->handle_packet(se,buf);

      */
    i++;
  }
  fprintf(stdout, "UDP_STREAMER: Closing streamer thread\n");
  pthread_exit(NULL);
}
int close_sendfile(void *opt_own, void *stats){
  //TODO
  //struct opts *spec_ops = (struct opts *)opt_own;
  //int err;
  //get_udp_stats(spec_ops,  stats);
  close_udp_streamer(opt_own, stats);

  return 0;
}
int sendfile_init_writer(struct opt_s *opt, struct streamer_entity *se)
{
  se->init = setup_udp_socket;
  se->start = sendfile_writer;
  se->close = udps_stop;
  se->opt = se->init(opt, se);
  se->stop = udps_stop;
  se->close_socket = sendfile_close_socket;
  return se->init(opt,se);
  }
