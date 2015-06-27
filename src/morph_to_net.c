#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <netdb.h>
#include <fcntl.h>
//#include <linux/fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/mman.h> /* For madvise */

#include "config.h"
#include "logging_main.h"
#include "timer.h"

#define DEFAULT_PORT 		2222
#define DEFAULT_PACKET_SIZE	5016
#define DEFAULT_BUFFERMULT	1024
#define B(x) (1l << x)

#define OPTS_DO_COUNTER		B(0)
#define OPTS_READING		B(1)
#define OPTS_DONE 		B(2)
#define OPTS_ERR_IN_SEND	B(3)
#define OPTS_ERR_IN_READ	B(4)

struct options{
  int port;
  int opts;
  int offset;
  uint64_t file_offset;
  uint64_t total_sent;
  uint64_t packet_size;
  uint64_t rate;
  uint64_t wait_nano;
  int waitms;
  int status;
  void * buffer;
  uint64_t n_ready;
  pthread_mutex_t access;
  pthread_cond_t wait;
  int buffermultiplier;
  char* target;
  char* filename;
};


#define LOCK do{if(pthread_mutex_lock(&(opts->access)) != 0){E("Error in lock");DO_EXIT}}while(0)
#define UNLOCK do{if(pthread_mutex_unlock(&(opts->access)) != 0) {E("Error in unlock"); DO_EXIT}}while(0)
#define WAIT do{if(pthread_cond_wait(&(opts->wait), &(opts->access)) != 0){E("Error in wait");DO_EXIT}}while(0)
#define SIGNAL do{if(pthread_cond_signal(&(opts->wait)) != 0){E("Error in signal");DO_EXIT}}while(0)

int usage(char * bin)
{
  LOG( "Usage: %s <file> <target> [OPTIONS]\n\
      -s <PORT>		Set target port to PORT \n\
      -c		Append mark5b packet counter \n\
      -a <BYTES>	Start after BYTES bytes \n\
      -f <OFFSET>	strip OFFSET from each packet \n\
      -r <RATE>		Send rate in Mb/s\n\
      -p <PACKET_SIZE>	Default 5016 \n", bin);
  exit(1);
}
#ifdef DO_EXIT
#undef DO_EXIT
#endif
#define DO_EXIT opts->opts &= ~OPTS_READING; opts->opts |= OPTS_ERR_IN_READ; return NULL;

void* start_reading(void* optss)
{
  D("Starting read");
  struct options* opts = (struct options*)optss;
  int err, fd;
  void * write_here = opts->buffer;
  uint64_t toread=0;
  int to_start=0;
  void * endofbuffer = opts->buffer + opts->buffermultiplier*opts->packet_size;

  fd = open(opts->filename, O_RDONLY, S_IRUSR);
  if(fd < 0){
    E("Error in open");
    opts->opts &= ~OPTS_READING;
    opts->opts |= OPTS_ERR_IN_READ;
  }

  if(opts->file_offset != 0)
  {
    long lerr = lseek(fd, opts->file_offset, SEEK_SET);
    if(lerr != (long)opts->file_offset){
      if(lerr < 0)
	E("Error in seek");
      else
	LOG("offset only %ld when wanted %ld\n", lerr, opts->file_offset);
    }
  }

  err = 0;
  D("Starting loop");
  do
  {
    LOCK;
    opts->n_ready+=err/opts->packet_size;

    if(to_start == 1)
    {
      to_start = 0;
      write_here = opts->buffer;
    }
    else
      write_here += err;
    if(err != 0)
      SIGNAL;

    if(opts->opts & OPTS_ERR_IN_SEND){
      E("Quitting cause error in send");
      UNLOCK;
      break;
    }

    while((toread = (opts->buffermultiplier - opts->n_ready)) == 0)
      WAIT;
    UNLOCK;
    /* Split the write */
    if(write_here + toread*opts->packet_size >= endofbuffer){
      to_start = 1;
      toread = (endofbuffer-write_here)/opts->packet_size;
    }
    err = read(fd, write_here, opts->packet_size*toread);
  }
  while(err > 0);
  D("Exiting");
  if(err == 0){
    D("End of file and all is well");
    LOCK;
    opts->n_ready+=toread;
    SIGNAL;
    UNLOCK;
  }
  else{
    E("Err in read");
    opts->opts &= ~OPTS_READING;
    opts->opts |= OPTS_ERR_IN_READ;
  }

  opts->opts &= ~OPTS_READING;
  opts->opts |= OPTS_DONE;
  
  return NULL;
}
#ifdef DO_EXIT
#undef DO_EXIT
#endif
#define DO_EXIT opts->opts &= ~OPTS_READING; opts->opts |= OPTS_ERR_IN_SEND; return -1;
int do_sending(struct options* opts)
{
  int fd, err;
  struct hostent* target;
  struct sockaddr_in serv_addr;
  uint64_t do_send=0;
  uint64_t sending = 0;
  uint64_t counter = 0;
  int64_t sleeptime;
  TIMERTYPE last_sent;
  TIMERTYPE now;
  TIMERTYPE sleepdo;
  sleepdo.tv_sec =0;
  void * read_here = opts->buffer;
  int to_start =0;
  void * endofbuffer = opts->buffer + opts->buffermultiplier*opts->packet_size;
  uint32_t *end_of_counter = ((uint32_t*)(&counter))+1;
  struct iovec *iovs = malloc(sizeof(struct iovec)*2);
  struct iovec *ciov = iovs;
  struct iovec *diov = iovs+1;
  int running = 1;
  if(opts->opts & OPTS_DO_COUNTER)
  {
    ciov->iov_base = &counter;
    ciov->iov_len = sizeof(uint64_t);
    (*end_of_counter) = 0;
  }
  memset(&serv_addr, 0, sizeof(struct sockaddr_in));

  target = gethostbyname(opts->target);
  CHECK_ERR_NONNULL_AUTO(target);

  serv_addr.sin_family = AF_INET;
  memcpy(&(serv_addr.sin_addr),target->h_addr_list[0], target->h_length);
  serv_addr.sin_port = htons(opts->port);

  D("Target is %s according to gethostbyname", target->h_addr_list[0]);
  fd = socket(AF_INET, SOCK_DGRAM, 0);
  CHECK_LTZ("Socket", fd);

  err = connect(fd, (struct sockaddr *) &serv_addr, sizeof(struct sockaddr));
  CHECK_ERR("Bind");

  diov->iov_len = opts->packet_size;
  GETTIME(last_sent);
  while(running == 1)
  {
    LOCK;
    opts->n_ready-=do_send;
    if(to_start == 1)
    {
      to_start = 0;
      read_here = opts->buffer;
    }
    
    if(opts->n_ready == 0 && !(opts->opts & OPTS_READING)){
      //running =0 ;
      break;
    }

    SIGNAL;
    
    while((do_send = opts->n_ready) == 0)
      WAIT;
    UNLOCK;
    if(read_here + do_send*opts->packet_size >= endofbuffer){
      to_start = 1;
      do_send = (endofbuffer-read_here)/opts->packet_size;
    }
    for(sending=0;sending < do_send;sending++)
    {
      diov->iov_base = read_here;
      if(opts->offset > 0)
	diov->iov_base +=opts->offset;

      GETTIME(now);
      sleeptime = opts->wait_nano - nanodiff(&last_sent, &now);
      if(sleeptime > 0)
      {
	sleepdo.tv_nsec = sleeptime;
	SLEEP_NANOS(sleepdo);
      }

      if(opts->opts & OPTS_DO_COUNTER)
      {
	err = writev(fd, iovs, 2);
	(*end_of_counter)++;
      }
      else
	err = writev(fd, diov, 1);
      if(err < 0){
	E("Error in stuffing to socket");
	opts->opts |= OPTS_ERR_IN_SEND;
	running = 0;
	break;
      }
      GETTIME(last_sent);
      opts->total_sent += err;
      read_here += opts->packet_size;
    }
  }
  free(iovs);

  err = shutdown(fd, SHUT_RDWR);
  CHECK_ERR("Shutdown");
  err = close(fd);
  CHECK_ERR("Close socket");

  return 0;
}
int main(int argc, char** argv)
{
  struct options * opts;
  opts = malloc(sizeof(struct options));
  memset(opts, 0,sizeof(struct options));
  int err,  ret, recalc_rate=0;
  pthread_t reader;
  opts->opts |= OPTS_READING;

  opts->port = DEFAULT_PORT;
  opts->packet_size = DEFAULT_PACKET_SIZE;
  opts->buffermultiplier = DEFAULT_BUFFERMULT;

  D("Getting opts");
  while((ret = getopt(argc, argv, "s:cf:p:r:a:")) != -1)
  {
    switch (ret){
      case 's':
	opts->port = atoi(optarg);
	if(opts->port < 1 || opts->port > 65536){
	  E("Cant set port to %d", opts->port);
	  usage(argv[0]);
	}
	break;
      case 'c':
	opts->opts |= OPTS_DO_COUNTER;
	break;
      case 'f':
	opts->offset = atoi(optarg);
	break;
      case 'a':
	opts->file_offset = atol(optarg);
	break;
      case 'r':
	opts->rate = atoi(optarg);
	recalc_rate=1;
	break;
      case 'p':
	opts->packet_size = atoi(optarg);
	if(opts->packet_size < 1 ||  opts->packet_size > 65536){
	  E("Cant set packet size to %ld", opts->packet_size);
	  usage(argv[0]);
	}
	break;
      default:
	E("Unknown parameter %c", ret);
	usage(argv[0]);
    }
  }
  D("Opts gotten. Checking parameters");
  if(argc -optind != 2){
    E("Wrong number of arguments. Expect 2, when got %d", argc-optind);
    usage(argv[0]);
  }
  argv +=optind;
  argc -=optind;

  if(recalc_rate==1)
    opts->wait_nano = ((1000000000)*opts->packet_size*8)/(opts->rate*1024*1024);
  D("Parameters checked");
  opts->filename = argv[0];
  opts->target = argv[1];

  pthread_mutex_init(&(opts->access), NULL);
  pthread_cond_init(&(opts->wait), NULL);

  opts->buffer = malloc(opts->packet_size*opts->buffermultiplier);

  err = pthread_create(&reader, NULL, &start_reading, opts);
  CHECK_ERR("reader create");

  err = do_sending(opts);
  CHECK_ERR("do sending");

  err = pthread_join(reader,NULL);
  CHECK_ERR("pthread_join");

  LOG("Sent %ld bytes\n)", opts->total_sent);

  pthread_mutex_destroy(&(opts->access));
  pthread_cond_destroy(&(opts->wait));

  free(opts->buffer);
  free(opts);

  return 0;
}
