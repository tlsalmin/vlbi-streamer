#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <limits.h>
#include <netdb.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <string.h>
#include <sys/types.h>
#include <ctype.h>
#define O(...) fprintf(stdout, "File: %s: ", argv[1]);fprintf(stdout, __VA_ARGS__)
#define LOG(...) fprintf(LOGTONORMAL, __VA_ARGS__)
#define LOGTONORMAL stdout
#define LOGTOERR stderr
#define E(str, ...)\
    do { fprintf(LOGTOERR,"ERROR: %s:%d:%s(): " str "\n",__FILE__,__LINE__,__func__ __VA_ARGS__ );perror("Error message"); } while(0)
#define B(x) (1l << x)
#define ITS_A_TCP_SOCKET B(0)
#define ITS_A_UDP_SOCKET B(1)
#define BACKLOG 1024
void usage(){
    LOG("Usage: check_64_seq <filename/socket> <byte spacing> [OPTIONS]\n");
    LOG("OPTIONS:\n \
	-t 		for tcp-socket\n \
	-u 		for tcp-socket\n \
	-w <wordsize>	for definining wordsize(default 8 bytes). Allowed 4 or 8\n\
	-o <length>	offset each read by length bytes\n\
	-b 		change endianess after read\n");

  exit(-1);
}
#define END_OF_FD -INT_MAX;

int do_read_file(int  fd, void* buffer, int  length)
{
  int err;
  err =read(fd, buffer, length);
  if(err < 0 ){
    E("Error in read of file");
    return -1;
  }
  if(err == 0)
    return END_OF_FD;
  return 0;
}
int do_read_socket(int fd, void* buffer, int  length)
{
  int err, startlength = length;
  while(length > 0)
  {
    err =recv(fd, buffer+(startlength-length), length, 0);
    if(err < 0 ){
      E("Error in read of file");
      return -1;
    }
    if(err == 0)
      return END_OF_FD;
    length-=err;
  }
  return 0;
}
int do_read_udpsocket(int fd, void* buffer, int  length)
{
  int err;
  err =recv(fd, buffer, length, 0);
  if(err < 0 ){
    E("Error in read of file");
    return -1;
  }
  if(err == 0)
    return END_OF_FD;
  if(err != length)
  {
    E("Wrong length packet received");
    return -1;
  }
  return 0;
}


int main(int argc, char** argv){
  int fd,sockfd=0,err;
  int opts =0;
  int64_t count = 0;
  int succesfull_socket=0;
  long spacing=0;
  int (*read_operator)(int, void*, int);
  int64_t read_count=0;
  int readoffset=0;
  int wordsize=8;
  int wordsizediff = 0;
  struct stat st;
  char* filename = NULL;
  void * buf;
  int bflag=0;
  char c;
  read_operator = do_read_file;
  while ((c = getopt (argc, argv, "w:bo:tu")) != -1)
    switch (c)
    {
      case 'w':
	wordsize = atoi(optarg);
	if(wordsize != 4 && wordsize != 8)
	{
	  usage();
	}
	break;
      case 'o':
	readoffset= atoi(optarg);
	break;
      case 't':
	opts |= ITS_A_TCP_SOCKET;
	read_operator = do_read_socket;
	break;
      case 'u':
	opts |= ITS_A_UDP_SOCKET;
	read_operator = do_read_udpsocket;
	break;
      case 'b':
	bflag = 1;
	break;
      case '?':
	if (optopt == 'w' || optopt == 'o')
	  fprintf (stderr, "Option -%c requires an argument.\n", optopt);
	else if (isprint (optopt)){
	  fprintf (stderr, "Unknown option `-%c'.\n", optopt);
	  usage(argv);
	}
	else{
	  fprintf (stderr,
	      "Unknown option character `\\x%x'.\n",
	      optopt);
	  usage(argv);
	}
      default:{
		abort ();
	      }
    }
  wordsizediff = 8 - wordsize;

  argc-=optind;
  argv+=optind;
  if(argc != 2)
    usage(argv);

  /*
  for (index = optind; index < argc; index++){
    printf("Non-option argument %s\n", argv[index]);
    if(j == 0){
    */
  filename = argv[0];
  if(opts & ITS_A_TCP_SOCKET)
    LOG("Socket");
  else
    LOG("Filename");
  LOG(" is %s\n", filename);

  spacing = atol(argv[1]);
  LOG("Spacing is %ld\n", spacing);

  if(filename == NULL || spacing == 0){
    fprintf(stderr,"No socket/filename or no spacing\n");
    return -1;
  }

  if(opts & (ITS_A_TCP_SOCKET|ITS_A_UDP_SOCKET))
  {
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr;
    socklen_t cliaddr_len;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    if(opts & ITS_A_TCP_SOCKET)
      hints.ai_socktype = SOCK_STREAM;
    else
      hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    err = getaddrinfo(NULL, filename, &hints, &servinfo);
    if(err != 0){
      E("getaddrinfo: %s",, gai_strerror(err));
    }
    for(p = servinfo; p!= NULL; p = p->ai_next)
    {
      int yes;
      sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
      if(sockfd < 0){
	E("Error in getting sockfd");
	continue;
      }
      /* Ok fine ill put reuseaddr. There has to be something fishy about it though.. */
      err = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
      if(err != 0)
      {
	E("Error in reuseaddr");
	exit(-1);
      }

      err = bind(sockfd, p->ai_addr, p->ai_addrlen);
      if(err != 0)
      {
	E("ERROR: bind socket");
	close(sockfd);
	continue;
      }
      succesfull_socket= 1;
      break;
    }
    if(succesfull_socket == 0){
      E("Coulnt even get socket");
      exit(-1);
    }
    succesfull_socket =0;
    freeaddrinfo(servinfo);
    if(opts & ITS_A_TCP_SOCKET)
    {
      err = listen(sockfd, BACKLOG);
      if(err != 0){
	E("Cant listen on socket");
	exit(-1);
      }
      fd = accept(sockfd, (struct sockaddr*)&their_addr, &cliaddr_len);
      if(fd < 0){
	E("Accept failed");
	exit(-1);
      }
    }
    else 
      fd = sockfd;
    succesfull_socket = 1;
  }
  else
  {
    if(stat(filename, &st) != 0){
      E("error in stat");
      exit(-1);
    }
    fd = open(filename, O_RDONLY);
    if(fd == -1){
      E("Error opening file %s",, filename);
      exit(-1);
    }
  }

  if(opts & ITS_A_TCP_SOCKET && succesfull_socket == 0){
    E("Didnt get a socket");
    exit(-1);
  }

  buf = malloc(spacing);
  if(buf == 0)
  {
    E("Error in buffer malloc");
    exit(-1);
  }

  err = read_operator(fd, buf, spacing);
  if(err< 0){
    E("Error in read");
    exit(-1);
  }

  memcpy((((void*)&count)+wordsizediff), buf+readoffset, wordsize);

  if(bflag == 0)
  {
    if(wordsize == 4)
      count = be32toh(*(((int32_t*)(&count))+1));
    else
      count = be64toh(count);
  }
  LOG("first count is %ld\n", count);
  count++;

  do
  {
    //for(i=1;i*spacing < fsize;i++){

    err = read_operator(fd,buf,spacing);
    if(err < 0){
      if(err == -INT_MAX)
	LOG("Orderly shutdown\n");
      else
	E("Error in read operation");
      break;
    }
    //lseek(fd, i*spacing+readoffset, SEEK_SET);

    memcpy((((void*)&read_count)+wordsizediff), buf+readoffset, wordsize);
    //memcpy(&read_count, buf+readoffset, wordsize);

    /*
       if(read(fd, &read_count,wordsize) < 0){
       O("Read error!\n");
       break;
       }
       */
    if(bflag==0){
      if(wordsize == 4)
	read_count = be32toh(*(((int32_t*)(&read_count))+1));
      else
	read_count = be64toh(read_count);
    }
    if(count != read_count){
      fprintf(stdout, "Discrepancy as count is %ld and read_count is %ld\n",count, read_count);
      //count = read_count;
    }
    count++;
  }
  while(err == 0);
    fprintf(stdout, "Done! Final count was %ld\n", count);

    if(opts & ITS_A_TCP_SOCKET){
      shutdown(fd, SHUT_RD);
      shutdown(sockfd, SHUT_RDWR);
      close(sockfd);
    }

    free(buf);

    if(close(fd) != 0){
      E("Error on close");
      exit(-1);
    }

    exit(0);
  }
