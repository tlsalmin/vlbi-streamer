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
#include <netdb.h>

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
#include "timer.h"

#ifdef LOG_TO_FILE
#undef LOG_TO_FILE
#define LOG_TO_FILE 0
#endif
#include "logging.h"
//#include "udp_stream.h"
//#include "timer.h"
//#include "streamer.h"

#define B(x) (1l << x)
#define UDP_SOCKET	B(0)
#define TCP_SOCKET	B(1)
#define SINGLEPORT	B(2)
#define RECVIT		B(3)

#define MAX_TARGETS 16
#define HOSTNAME_MAXLEN    256
size_t sendbuffer;
pthread_rwlock_t rwl;
volatile int running;
void * buf;
  int streams;

struct sillystruct{
  int fd;
  int seq;
  pthread_t pt;
  uint64_t sent;
};
#define O(...) fprintf(stdout, __VA_ARGS__)
#define STARTPORT 2222

int def=0, defcheck=0, len=0, packet_size=0;
int opts;
/* Cleaned up from round 1	 	*/
int connect_to_c(const char* t_target,const char* t_port, int * fd)
{
  int err;
  struct addrinfo hints, *res, *p;
  memset(&hints, 0,sizeof(struct addrinfo));

  if(opts & TCP_SOCKET)
    hints.ai_socktype = SOCK_STREAM;
  else
    hints.ai_socktype = SOCK_DGRAM;
  int gotthere;
  hints.ai_family = AF_UNSPEC;
  hints.ai_flags = AI_PASSIVE;
  err = getaddrinfo(t_target, t_port, &hints, &res);
  if(err != 0){
    E("Error in getaddrinfo: %s for %s:%s",, gai_strerror(err), t_target, t_port);
    return -1;
  }
  for(p = res; p != NULL; p = p->ai_next)
  {
    gotthere=0;
    *fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if(*fd < 0)
    {
      E("Socket to client");
      continue;
    }

    if(opts & RECVIT)
    {
      err = bind(*fd, p->ai_addr, p->ai_addrlen);
      if(err != 0)
      {
	close(*fd);
	E("bind");
	continue;
      }
      if(opts & TCP_SOCKET)
      {
	D("called listen");
	if(opts & SINGLEPORT)
	  err = listen(*fd, streams);
	else
	  err = listen(*fd, 1);
	if(err != 0)
	{
	  close(*fd);
	  E("listen");
	  continue;
	}
	/*
	   int testfd = accept(*fd, NULL, NULL);
	   D("Hur");
	   */
      }
      if(opts & RECVIT)
      {
	int yes = 1;
	err = setsockopt(*fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
      }
    }
    else
    {
      err = connect(*fd, p->ai_addr, p->ai_addrlen);
      if(err < 0)
      {
	close(*fd);
	E("Connect to client");
	continue;
      }
    }
    gotthere=1;
    break;
  }
  if(gotthere ==0)
    return -1;

  freeaddrinfo(res);
  if (def == 0)
  {
    def = packet_size;
    len = sizeof(def);
    while(err == 0){
      //O("RCVBUF size is %d",,def);
      def  = def << 1;
      if(opts & RECVIT)
	err = setsockopt(*fd, SOL_SOCKET, SO_RCVBUF, &def, (socklen_t) len);
      else
	err = setsockopt(*fd, SOL_SOCKET, SO_SNDBUF, &def, (socklen_t) len);
      if(err == 0){
	//O("Trying SNDBUF size %d\n", def);
      }
      if(opts & RECVIT)
	err = getsockopt(*fd, SOL_SOCKET, SO_RCVBUF, &defcheck, (socklen_t * )&len);
      else
	err = getsockopt(*fd, SOL_SOCKET, SO_SNDBUF, &defcheck, (socklen_t * )&len);
      if(defcheck != (def << 1)){
	O("Limit reached. Final size is %d Bytes\n",defcheck);
	def = defcheck;
	break;
      }
    }
  }
  else{
    if(opts & RECVIT)
      err = setsockopt(*fd, SOL_SOCKET, SO_RCVBUF, &def, (socklen_t) len);
    else
      err = setsockopt(*fd, SOL_SOCKET, SO_SNDBUF, &def, (socklen_t) len);
    if(err!= 0)
      E("Error in setting SO_SNDBUF");
  }


  return 0;
}

void usage()
{
  O("USAGE: groupsend <comma separated list of targets> <streams> <packet_size> <time>\n");
  exit(-1);
}

void * sendthread(void * optsi)
{
  int err;
  struct sillystruct* st = (struct sillystruct*)optsi;
  int fd = st->fd;
  if(opts & TCP_SOCKET && opts & RECVIT){
    D("Waiting for accept on socket %d",, fd);
    if(opts & SINGLEPORT)
      pthread_rwlock_wrlock(&rwl);
    fd = accept(fd, NULL, NULL);
    if(opts & SINGLEPORT)
      pthread_rwlock_unlock(&rwl);
    D("And we have accept");
  }
  while(running == 1){
    if(opts & RECVIT)
      err = recv(fd, buf,packet_size, 0);
    else
      err = send(fd, buf,sendbuffer, 0);

    if(err < 0){
      perror("Sending");
      running = 0;
      break;
    }
    else if(err == 0){
      O("Socket shutdown");
      running = 0;
      break;
    }
    else{
      pthread_rwlock_rdlock(&rwl);
      st->sent += err;
      pthread_rwlock_unlock(&rwl);
    }
  }
  pthread_exit(NULL);
}

int main(int argc, char** argv){
  char * targets[MAX_TARGETS], *port;
  TIMERTYPE tval_start, tval,sleep,tval_temp;
  int runtime;
  int mainfd=0;
  int portn;
  int n_targets=0;
  int ret;
  uint64_t total=0;
  uint64_t total_last=0;
  pthread_rwlock_init(&rwl, NULL);
  running = 1;
  opts = 0;


  ZEROTIME(sleep);
  ZEROTIME(tval_start);
  ZEROTIME(tval_temp);
  ZEROTIME(tval);

  opts |= UDP_SOCKET;
  while((ret = getopt(argc, argv, "tp:sr")) != -1)
  {
    switch (ret){
      case 't':
	opts &= ~UDP_SOCKET;
	opts |= TCP_SOCKET;
	break;
      case 's':
	opts |= SINGLEPORT;
	break;
      case 'r':
	opts |= RECVIT;
	break;
      case 'p':
	portn = atoi(optarg);
	if(portn <= 0 || portn >= 65536){
	  E("Illegal port %d",, portn);
	  usage(argv[0]);
	}
	break;
      default:
	E("Unknown parameter %c",, ret);
	usage(argv[0]);
    }
  }
  argv+=(optind-1);
  argc-=(optind-1);

  if(argc != 5)
    usage();

  char * temp = argv[1], *temp2;
  int i=0, err = 0;
  while((temp2 = index(temp, ',')) != NULL){
    targets[n_targets] = malloc(sizeof(char)*HOSTNAME_MAXLEN);
    memset(targets[n_targets], 0, sizeof(char)*HOSTNAME_MAXLEN);
    memcpy(targets[n_targets], temp, temp2-temp);
    n_targets++;
    temp = temp2+1;
  }
  targets[n_targets] = temp;
  n_targets++;
  if(opts & RECVIT)
    portn = atoi(argv[1]);
  for(i=0;i<n_targets;i++)
  {
    O("Target %d is %s\n", i, targets[i]);
  }
  streams = atoi(argv[2]);
  packet_size = atoi(argv[3]);

  runtime = atoi(argv[4]);
  struct sillystruct* st = malloc(sizeof(struct sillystruct)*streams);
  memset(st, 0,sizeof(struct sillystruct)*streams);

  //O("Starting %d streams to %s with packet size %d\n", streams, target, packet_size);

  sendbuffer = packet_size;

  buf = malloc(sendbuffer);

  port = malloc(sizeof(char)*8);

  O("Connecting");
  for(i=0;i<streams;i++){
    memset(port, 0, sizeof(char)*8);
    if(opts & SINGLEPORT)
      sprintf(port, "%d", portn);
    else
      sprintf(port, "%d", portn+i);
    if(opts & RECVIT)
    {
      if(opts & SINGLEPORT)
      {
	if(mainfd == 0)
	{
	  err = connect_to_c(NULL, port, &mainfd);
	  CHECK_ERR("connect");
	}
	st[i].fd = mainfd;
      }
      else
      {
	err = connect_to_c(NULL, port, &st[i].fd);
	CHECK_ERR("Connect");
      }
    }
    else
    {
      err = connect_to_c(targets[i%n_targets], port, &st[i].fd);
      if(err != 0){
	E("Error in connect");
	running=0;
	break;
      }
    }
    st[i].seq = i;
  }
  O("Streams done\n");

  if(running == 1)
  {
    GETTIME(tval_start);

    O("Into send-loop\n");
    for(i=0;i<streams;i++)
    {
      err = pthread_create(&st[i].pt, NULL, sendthread,  &st[i]);
      if(err != 0){
	perror("create thread");
	running=0;
	break;
      }
    }
    COPYTIME(tval_start, tval_temp);
    while(running==1)
    {
      usleep(1000000);
      pthread_rwlock_wrlock(&rwl);
      for(i=0;i<streams;i++)
      {
	total+=st[i].sent;
      }
      GETTIME(tval);
      pthread_rwlock_unlock(&rwl);
      O("%ld %0.2Lf\n", get_sec_diff(&tval_start,&tval), (((long double)total-(long double)total_last)*((long double)8))/(((long double)(1024*1024))*((long double)nanodiff(&tval_temp, &tval)/((long double)BILLION))));
      total_last = total;
      total = 0;
      if(GETSECONDS(tval)-GETSECONDS(tval_start) > runtime)
      {
	O("All done\n");
	running =0;
      }
      COPYTIME(tval, tval_temp);
    }
    total_last = 0;
    for(i=0;i<streams;i++)
    {
      shutdown(st[i].fd, SHUT_RDWR);
      close(st[i].fd);
      pthread_join(st[i].pt, NULL);
      total_last += st[i].sent;
    }
  }

  O("All done and final values are %ld bytes sent and speed was %0.2Lf Mb/s\n", total_last, ((long double)total_last*8)/((long double)(1024*1024)*((long double)get_sec_diff(&tval_start, &tval))));

  pthread_rwlock_destroy(&rwl);
  free(buf);
  for(i=0;i<n_targets-1;i++)
  {
    free(targets[i]);
  }
  return 0;
}
