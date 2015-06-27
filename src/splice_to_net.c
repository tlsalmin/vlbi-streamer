#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/sendfile.h>
//#include <linux/fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/mman.h> /* For madvise */

#include "config.h"
#include "logging_main.h"

#define DEFAULT_PORT 		2222
#define DEFAULT_PACKET_SIZE	5016
#define B(x) (1l << x)
#define MAX_PIPE_SIZE 1048576

#define OPTS_DO_COUNTER	B(0)
#define TCP_MODE	B(1)

struct options{
  int port;
  int opts;
  int offset;
  int packet_size;
  int rate;
  int waitms;
  int maxbytes_inpipe;
  char* target;
  char* filename;
};

int usage(char * bin)
{
  LOG( "Usage: %s <file> <target> [OPTIONS]\n\
      -s <PORT>		Set target port to PORT \n\
      -c		Append mark5b packet counter \n\
      -t		TCP connection \n\
      -f <OFFSET>	strip OFFSET from each packet \n\
      -r <RATE>		Send rate in Mb/s\n\
      -p <PACKET_SIZE>	Default 5016 \n", bin);
  exit(1);
}
int read_file_to_pipe(struct options* opts,int pipe)
{
  int err, fd;
  ssize_t count;

  fd = open(opts->filename, O_RDONLY, S_IRUSR);
  CHECK_LTZ("open", fd);

  D("Splicing it up in open");
  do
  {
    count = splice(fd, NULL, pipe, NULL, opts->maxbytes_inpipe, SPLICE_F_MOVE|SPLICE_F_MORE);
  }while(count > 0);

  D("Done splicing");
  CHECK_LTZ("final count", count);

  err = close(pipe);
  CHECK_ERR("Close pipe");
  err = close(fd);
  CHECK_ERR("Close fd");
  return 0;
}
int splice_to_socket(struct options* opts,int  pipe)
{
  int fd, err, dummyfd;
  ssize_t count;
  struct hostent* target;
  struct sockaddr_in serv_addr;
  void *dummybuffer = NULL;
  uint64_t counter = 0;
  uint32_t *end_of_counter = ((uint32_t*)(&counter))+1;
  struct iovec ciov;
  if(opts->opts & OPTS_DO_COUNTER)
  {
    ciov.iov_base = &counter;
    ciov.iov_len = sizeof(uint64_t);
  }
  if (opts->offset != 0){
    dummybuffer = malloc(opts->offset);
  }
  dummyfd = open("/dev/null", O_WRONLY);
  CHECK_LTZ("dummyfd", dummyfd);

  memset(&serv_addr, 0, sizeof(struct sockaddr_in));

  target = gethostbyname(opts->target);
  CHECK_ERR_NONNULL_AUTO(target);

  serv_addr.sin_family = AF_INET;
  memcpy(&(serv_addr.sin_addr),target->h_addr_list[0], target->h_length);
  serv_addr.sin_port = htons(opts->port);

  D("Target is %s according to gethostbyname", target->h_addr_list[0]);
  if(opts->opts & TCP_MODE)
    fd = socket(AF_INET, SOCK_STREAM, 0);
  else
    fd = socket(AF_INET, SOCK_DGRAM, 0);
  CHECK_LTZ("Socket", fd);

  err = connect(fd, (struct sockaddr *) &serv_addr, sizeof(struct sockaddr));
  CHECK_ERR("Bind");

  long totransfer=opts->packet_size-1;
  size_t countermon;

  if(opts->opts & OPTS_DO_COUNTER)
  {
    D("Doing the counter one time!");
    countermon = vmsplice(pipe, &ciov, 1, SPLICE_F_GIFT);
    if(countermon != ciov.iov_len)
      E("Counter didn't write %ld bytes", ciov.iov_len);
    (*end_of_counter)++;
  }

    do
    {
      count = splice(pipe, NULL, fd, NULL, totransfer, SPLICE_F_MORE);
      if(count <= 0)
      {
	D("Err or exit in original splice");
	break;
      }
      totransfer-=count;

      /* Check if we have a full package */
      if(totransfer == 0){
	/* I know i know.. Not very elegant! */
	count = splice(pipe, NULL, fd, NULL, 1, 0);
	if(count < 0){
	  E("error in finishing splice");
	  break;
	}
	if(count == 0){
	  D("Pipe closed");
	  break;
	}
	if(opts->opts & OPTS_DO_COUNTER)
	{
	  vmsplice(pipe, &ciov, 1, SPLICE_F_MORE);
	  (*end_of_counter)++;
	}
	totransfer=opts->packet_size-1;
	count = 1;
      }
    }while(count > 0);

  if(count < 0)
    E("Error in splice to socket");

  if(dummybuffer != NULL)
    free(dummybuffer);

  err = shutdown(fd, SHUT_RDWR);
  CHECK_ERR("Shutdown");
  err = close(fd);
  CHECK_ERR("Close socket");
  err = close(pipe);
  CHECK_ERR("Close pipe");
  err = close(dummyfd);
  CHECK_ERR("dummyfd");
  return 0;
}

int main(int argc, char** argv)
{
  struct options * opts;
  opts = malloc(sizeof(struct options));
  memset(opts, 0,sizeof(struct options));
  int err, pipes[2], retval=0, ret, childpid=0;

  opts->port = DEFAULT_PORT;
  opts->packet_size = DEFAULT_PACKET_SIZE;

  D("Getting opts");
  while((ret = getopt(argc, argv, "s:cf:p:r:t")) != -1)
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
      case 't':
	opts->opts |= TCP_MODE;
	break;
      case 'p':
	opts->packet_size = atoi(optarg);
	if(opts->packet_size < 1 ||  opts->packet_size > 65536){
	  E("Cant set packet size to %d", opts->packet_size);
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
  //argc -=optind;

  D("Parameters checked");
  opts->filename = argv[0];
  opts->target = argv[1];

  err = pipe(pipes);
  CHECK_ERR("pipe");

#ifdef F_SETPIPE_SZ
  err = fcntl(pipes[1], F_SETPIPE_SZ, MAX_PIPE_SIZE);
  CHECK_LTZ("fcntl", err);
  opts->maxbytes_inpipe = fcntl(pipes[1], F_GETPIPE_SZ);
#else
  opts->maxbytes_inpipe = 65536;
#endif
  D("Maximum pipe size set to %d", opts->maxbytes_inpipe);

  D("Target is %s and filename %s", opts->target, opts->filename);

  childpid = fork();

  if(childpid == 0){
    D("Child forked");
    close(pipes[0]);
    err = read_file_to_pipe(opts, pipes[1]);
    if(err != 0){
      E("Error in read file to pipe");
      _exit(EXIT_FAILURE);
    }
    D("Child exit");
    _exit(EXIT_SUCCESS);
  }
  else
  {
    close(pipes[1]);
    err = splice_to_socket(opts, pipes[0]);
    if(err != 0){
      E("Error in splicing");
      retval = -1;
    }
    D("Waiting for childpid %d", childpid);
    if(waitpid(childpid, &retval, WNOHANG) == 0)
    {
      D("Pid not exited. Lets close the other pipe end. Maybe it'll wake up");
      close(pipes[0]);
    }
    waitpid(childpid, &retval, 0);
    D("Ok now it exited");
  }

  return retval;
}
