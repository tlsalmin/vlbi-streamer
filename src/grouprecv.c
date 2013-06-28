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

#define B(x) (1l << x)
#define UDP_SOCKET	B(0)
#define TCP_SOCKET	B(1)
#define SINGLEPORT	B(2)

#define O(...) fprintf(stdout, __VA_ARGS__)

#define STARTPORT 2222
#define MAX_TARGETS 16
#define HOSTNAME_MAXLEN    256
pthread_rwlock_t rwl;
volatile int running;
volatile int got_accept=0;
int streams;
void * buf;

struct sillystruct{
  int fd;
  int seq;
  pthread_t pt;
  uint64_t sent;
};

int def=0, defcheck=0, len=0, packet_size=0;
int opts;
/* Cleaned up from round 1	 	*/
int connect_to_c(const char* t_port, int * fd)
{
  int err;
  struct addrinfo hints, *res, *p;
  memset(&hints, 0,sizeof(struct addrinfo));
  O("Creating socket for port %s\n", t_port);

  if(opts & TCP_SOCKET)
    hints.ai_socktype = SOCK_STREAM;
  else
    hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE;
  int gotthere;
  hints.ai_family = AF_UNSPEC;
  err = getaddrinfo(NULL, t_port, &hints, &res);
  if(err != 0){
    E("Error in getaddrinfo: %s for %s",, gai_strerror(err),  t_port);
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

    /*
    int yes = 1;
    err = setsockopt(*fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
    */
    err = bind(*fd, p->ai_addr, p->ai_addrlen);
    if(err < 0)
    {
      close(*fd);
      E("bind");
      continue;
    }
    if(opts&TCP_SOCKET)
    {
      D("called listen");
      if(opts & SINGLEPORT)
	err = listen(*fd, streams);
      else
	err = listen(*fd, 1);
      if(err < 0)
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
      err = setsockopt(*fd, SOL_SOCKET, SO_RCVBUF, &def, (socklen_t) len);
      if(err == 0){
	//O("Trying SNDBUF size %d\n", def);
      }
      err = getsockopt(*fd, SOL_SOCKET, SO_RCVBUF, &defcheck, (socklen_t * )&len);
      if(defcheck != (def << 1)){
	O("Limit reached. Final size is %d Bytes\n",defcheck);
	def = defcheck;
	break;
      }
    }
  }
  else{
    err = setsockopt(*fd, SOL_SOCKET, SO_RCVBUF, &def, (socklen_t) len);
    if(err!= 0)
      E("Error in setting SO_SNDBUF");
  }

  D("All ok. got fd %d",, *fd);

  return 0;
}

void usage()
{
  O("USAGE: grouprecv <starting_port> <streams> <packet_size> <time>\n");
  exit(-1);
}

void * recvthread(void * optsi)
{
  int err;
  int fd=0;
  struct sillystruct* st = (struct sillystruct*)optsi;
  D("Starting receive on fd %d",, st->fd);
  if(opts & TCP_SOCKET){
    D("Waiting for accept");
    if(opts & SINGLEPORT)
      pthread_rwlock_wrlock(&rwl);
    fd = accept(st->fd, NULL, NULL);
    if(opts & SINGLEPORT)
      pthread_rwlock_unlock(&rwl);
    D("And we have accept");
  }
  else
    fd = st->fd;
  if(fd <=0){
    perror("accept");
    running=0;
    pthread_exit(NULL);
  }
  got_accept=1;
  while(running == 1){
    err = recv(fd, buf,packet_size, 0);
    if(err < 0){
      perror("Receiving");
      running = 0;
      break;
    }
    else if(err == 0){
      O("Socket shutdown\n");
      running = 0;
      break;
    }
    pthread_rwlock_rdlock(&rwl);
    st->sent += err;
    pthread_rwlock_unlock(&rwl);
  }
  D("exiting receive thread");
  pthread_exit(NULL);
}

int main(int argc, char** argv){
  char *port;
  TIMERTYPE tval_start, tval,sleep,tval_temp;
  int runtime;
  int mainfd=0;
  int portn;
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
  while((ret = getopt(argc, argv, "ts")) != -1)
  {
    switch (ret){
      case 't':
	opts &= ~UDP_SOCKET;
	opts |= TCP_SOCKET;
	break;
      case 's':
	opts |= SINGLEPORT;
	break;
      default:
	E("Unknown parameter %c",, ret);
	usage(argv[0]);
	break;
    }
  }
  argv+=(optind-1);
  argc-=(optind-1);

  if(argc != 5)
    usage();

  int i=0, err = 0;

  portn = atoi(argv[1]);
  streams = atoi(argv[2]);
  packet_size = atoi(argv[3]);

  runtime = atoi(argv[4]);
  struct sillystruct* st = malloc(sizeof(struct sillystruct)*streams);
  memset(st, 0,sizeof(struct sillystruct)*streams);

  O("Starting %d streams receiving from port %d with packet size %d\n", streams, portn, packet_size);


  buf = malloc(packet_size);

  port = malloc(sizeof(char)*8);

  O("Connecting");
  for(i=0;i<streams;i++){
    memset(port, 0, sizeof(char)*8);
    sprintf(port, "%d", portn+i);
    if(opts & SINGLEPORT)
    {
      if (mainfd == 0)
      {
	err = connect_to_c( port, &mainfd);
	CHECK_ERR("connect");
      }
      st[i].fd = mainfd;
    }
    else{
      err = connect_to_c( port, &st[i].fd);
      CHECK_ERR("Connect");
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
      err = pthread_create(&st[i].pt, NULL, recvthread,  &st[i]);
      CHECK_ERR("Thread create");
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
      if(got_accept ==0){
	runtime++;
	GETTIME(tval_start);
      }
      if(GETSECONDS(tval)-GETSECONDS(tval_start) > runtime)
      {
	O("All done\n");
	running =0;
      }
      COPYTIME(tval, tval_temp);
    }
    for(i=0;i<streams;i++)
    {
      shutdown(st[i].fd, SHUT_RDWR);
      close(st[i].fd);
      pthread_join(st[i].pt, NULL);
    }
  }

  O("All done and final values are %ld bytes sent and speed was %0.2Lf Mb/s\n", total_last, ((long double)total_last*8)/((long double)(1024*1024)*((long double)get_sec_diff(&tval_start, &tval))));

  pthread_rwlock_destroy(&rwl);
  free(buf);
  return 0;
}
