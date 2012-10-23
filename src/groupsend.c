#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "config.h"
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
#include <sys/mman.h> //for MMAP and poll
#include <sys/poll.h>

#include <pthread.h>
#include <assert.h>

#ifdef HAVE_RATELIMITER
#include <time.h> 
#endif
#include <unistd.h>

#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <arpa/inet.h>
#ifdef HAVE_LINUX_NET_TSTAMP_H
#include <linux/net_tstamp.h>
#endif
#include <netinet/in.h>
#include <endian.h>
#include "udp_stream.h"
#include "timer.h"
#include "streamer.h"

#define STREAMS 32
#define STARTPORT 2222
#define PACKET_SIZE	8888
#define TARGET_IP "192.168.0.3"

int main(int argc, char** argv){
  (void)argc;
  (void)argv;
  
  int * sockets = (int*)malloc(sizeof(int)*STREAMS);
  struct sockaddr_in * sin = (struct sockaddr_in*)malloc(sizeof(struct sockaddr_in)*STREAMS);
  void* buf = malloc(PACKET_SIZE);


  int i, err = 0;
  TIMERTYPE now,sleep;
  ZEROTIME(now);
  ZEROTIME(sleep);
  D("Zeroed sleep\n");
  /*
  D("Doing the double sndbuf-loop");
  int def = PACKET_SIZE;
  
  while(err == 0){
    //D("RCVBUF size is %d",,def);
    def  = def << 1;
    err = setsockopt(*fd, SOL_SOCKET, SO_SNDBUF, &def, (socklen_t) len);
    if(err == 0){
      D("Trying SNDBUF size %d",, def);
    }
    err = getsockopt(*fd, SOL_SOCKET, SO_SNDBUF, &defcheck, (socklen_t * )&len);
    if(defcheck != (def << 1)){
      D("Limit reached. Final size is %d Bytes",,defcheck);
      break;
    }
    }
    */
  struct hostent *hostptr;

  hostptr = gethostbyname(TARGET_IP);
  if(hostptr == NULL){
    perror("Hostname");
    exit(-1);
  }

  D("Resolved hostname");

  D("Setting up streams\n");
  for(i=0;i<STREAMS;i++){
    sockets[i] = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&(sin[i]), 0, sizeof(struct sockaddr_in));   
    sin[i].sin_family = AF_INET;           
    sin[i].sin_port = htons(STARTPORT+i);    
    memcpy(&(sin[i].sin_addr.s_addr), (char *)hostptr->h_addr, sizeof(sin[i].sin_addr.s_addr));
  }
  D("Streams done\n");

  D("Getting clock\n");
  GETTIME(now);


  SETNANOS(sleep,400*1000);
  while(err == 0){
    for(i=0;i<STREAMS;i++){
      err = sendto(sockets[i], buf,PACKET_SIZE, 0, &sin[i],sizeof(struct sockaddr_in));
    }
    if(err != 0)
      perror("hur");
  }

  free(buf);
  /*
     for(i=0;i<STREAMS;i++){
     shutdown(&sockets[i], SHUT_RDWR);
     }
     */
  free(sockets);
  free(sin);

  D("Exit OK\n");
  return 0;
}
