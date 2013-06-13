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

#include "../src/datatypes_common.h"
#define LOG_TO_FILE 0
#define BACKLOG 1024
#define DEBUG_OUTPUT 0
#include "../src/logging.h"
#include "vbs_metadata.h"
#include "metadata_seqnum.h"
#include "metadata_mark5b.h"
#include "metadata_vdif.h"

void usage(char * bin)
{
  LOG("%s Inspects metadata in VLBI related data from arbitrary sources\n\n\
      Usage:\t%s [OPTION]... FILE\n\
      Or:\t%s [OPTION]... PORT\n\n\
      Options: \n\
      -q <datatype>\t A datatype(udpmon, mark5b, mark5bnet, vdif,unknown)\n\
      -t <listener>\t A listener type(tcpstream,udpstream,file)\n\
      -m <traverser>\t A type of traversing(check,output,vim)\n\
      -e <errors>\t Max number of errors before exit (default: 30)\n\
      -p <size>\t\t A packet size\n", bin, bin,bin);
}

struct stat * fileinfo;

unsigned int get_mask(int start, int end){
  unsigned int returnable = 0;
  while(start <= end){
    returnable |= B(start);
    start++;
  }
  return returnable;
}
void cleanup_tcpstream(struct common_control_element* cce)
{
  shutdown(cce->fd, SHUT_RDWR);
  close(cce->fd);
}
void cleanup_udpstream(struct common_control_element* cce)
{
  shutdown(cce->fd, SHUT_RD);
  close(cce->fd);
}
int do_read_file(int  fd, void* buffer, int  length, int count)
{
  int templength, err;
  /* Inefficient oh no	*/
  if(!(count > 0))
  {
    if(count < 0)
    {
      if(lseek(fd, (count-1)*length, SEEK_CUR) == (off_t)-1) 
	lseek(fd, 0, SEEK_SET);
    }
    if(count ==0){
      LOG("Seeking to start of file\n");
      lseek(fd,0,SEEK_SET);
    }
    else if (count == INT_MAX)
    {
      if(fd == 0){
	E("Cant seek to end of pipe");
      }
      else{
	/* Doing this to preserve alignment when reversing	*/
	LOG("Seeking to end of file\n");
	long maxseek = (fileinfo->st_size/length)*length;
	lseek(fd, maxseek, SEEK_SET);
      }
    }
    count = 1;
  }
  while(count > 0)
  {
    templength = length;
    while(templength > 0)
    {
      err =read(fd, buffer+(length-templength), templength);
      if(err < 0 ){
	E("Error in read of file");
	return -1;
      }
      if(err == 0){
	return END_OF_FD;
      }
      templength -= err;
    }
    count--;
  }
  return 0;
}
int do_read_socket(int fd, void* buffer, int  length, int count)
{
  int err, templength;
  while(count > 0)
  {
    templength = length;
    while(templength > 0)
    {
      err =recv(fd, buffer+(length-templength), templength, 0);
      if(err < 0 ){
	E("Error in read of file");
	return -1;
      }
      if(err == 0)
	return END_OF_FD;
      templength-=err;
    }
    count--;
  }
  return 0;
}
int do_read_udpsocket(int fd, void* buffer, int  length, int count)
{
  int templength;
  int err;
  while(count > 0)
  {
    templength = length;
    while(templength > 0)
    {
      err =recv(fd, buffer+(length-templength), templength, 0);
      if(err < 0 ){
	E("Error in read of file");
	return -1;
      }
      if(err == 0)
	return END_OF_FD;
      /*
      if(err != length)
      {
	E("Wrong length packet received");
	return -1;
      }
      */
      templength+=err;
    }
    count--;
  }
  return 0;
}
int handle_open_socket(struct common_control_element* cce)
{
  int err;
  struct addrinfo hints, *servinfo, *p;
  struct sockaddr_storage their_addr;
  int succesfull_socket = 0;
  socklen_t cliaddr_len;

  if(cce->port_or_filename == NULL){
    E("No port given");
    return -1;
  }

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  if(cce->optbits & LISTEN_TCPSOCKET)
    hints.ai_socktype = SOCK_STREAM;
  else
    hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE;

  err = getaddrinfo(NULL, cce->port_or_filename, &hints, &servinfo);
  if(err != 0){
    E("getaddrinfo: %s",, gai_strerror(err));
  }
  for(p = servinfo; p!= NULL; p = p->ai_next)
  {
    int yes;
    cce->sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if(cce->sockfd < 0){
      E("Error in getting sockfd");
      continue;
    }
    // Ok fine ill put reuseaddr. There has to be something fishy about it though.. 
    err = setsockopt(cce->sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
    if(err != 0)
    {
      E("Error in reuseaddr");
      exit(-1);
    }
    err = bind(cce->sockfd, p->ai_addr, p->ai_addrlen);
    if(err != 0)
    {
      E("ERROR: bind socket");
      close(cce->sockfd);
      continue;
    }
    succesfull_socket= 1;
    break;
  }
  if(succesfull_socket == 0){
    E("Coulnt even get socket");
    return -1;
  }
  freeaddrinfo(servinfo);
  int def = cce->framesize;
  int len,defcheck=0;
  len = sizeof(def);

  while(err == 0){
    //D("RCVBUF size is %d",,def);
    def  = def << 1;
    err = setsockopt(cce->sockfd, SOL_SOCKET, SO_RCVBUF, &def, (socklen_t) len);
    if(err == 0){
      D("Trying RCVBUF size %d",, def);
    }
    err = getsockopt(cce->sockfd, SOL_SOCKET, SO_RCVBUF, &defcheck, (socklen_t * )&len);
    if(defcheck != (def << 1)){
      D("Limit reached. Final size is %d Bytes",,defcheck);
      break;
    }
  }

  if(cce->optbits & LISTEN_TCPSOCKET)
  {
    err = listen(cce->sockfd, BACKLOG);
    if(err != 0){
      E("Cant listen on socket");
      return -1;
    }
    cce->fd = accept(cce->sockfd, (struct sockaddr*)&their_addr, &cliaddr_len);
    if(cce->fd < 0){
      E("Accept failed");
      return -1;
    }
  }
  else 
    cce->fd = cce->sockfd;

  if(cce->optbits & LISTEN_TCPSOCKET){
    cce->packet_move = do_read_socket;
    cce->cleanup_reader = cleanup_tcpstream;
  }
  else{
    cce->packet_move = do_read_udpsocket;
    cce->cleanup_reader = cleanup_udpstream;
  }

  return 0;
}
void cleanup_filereader(struct common_control_element* cce)
{
  close(cce->fd);
}
void fd_reset(struct common_control_element*cce)
{
  lseek(cce->fd, -(cce->framesize+(fileinfo->st_size % cce->framesize)), SEEK_END);
  cce->packet_move(cce->fd, cce->buffer, cce->framesize, 1);
}
int handle_open_file(struct common_control_element* cce)
{
  int err;
  //struct stat* st;
  cce->packet_move = do_read_file;
  if(*(cce->port_or_filename) == '-')
  {
    /* Stdin	*/
    cce->fd = 0;
    if(cce->optbits & (TRAVERSE_VIM))
    {
      E("Cant VIM with stdin. Use |less");
      return -1;
    }
  }
  else if(cce->port_or_filename == NULL)
  {
    E("No filename given");
    return -1;
  }
  else
  {
    fileinfo = malloc(sizeof(struct stat));
    //fileinfo = cce->listen_metadata;
    err = stat(cce->port_or_filename, fileinfo);
    CHECK_ERR("Stat");
    cce->fd = open(cce->port_or_filename, O_RDONLY);
    if(cce->fd <0){
      E("Error in open");
      return -1;
    }
    cce->reset_to_last_known_good = fd_reset;
  }
  cce->cleanup_reader = cleanup_filereader;
  return 0;
}

int keyboardinput(struct common_control_element * cce){
  char dachar;
  int err;
#ifndef PORTABLE
  err = system ("/bin/stty raw");
  if(err < 0){
    E("Error in system");
    return -1;
  }
#endif
  dachar = getchar();
  switch(dachar)
  {
    case (int)'q': 
      cce->running = 0;
      break;
    case (int)'h': 
      if(cce->optbits & HEXMODE){
	if(cce->hexoffset>0)
	  cce->hexoffset--;
      }
      else{
	err = cce->packet_move(cce->fd, cce->buffer, cce->framesize, -1);
	cce->metadata_increment(cce, -1);
      }
      break;
    case (int)'k': 
      if(cce->optbits & HEXMODE){
	if(cce->hexoffset>JUMPSIZE)
	  cce->hexoffset-=JUMPSIZE;
      }
      else{
	err = cce->packet_move(cce->fd, cce->buffer, cce->framesize, -JUMPSIZE);
	cce->metadata_increment(cce, -JUMPSIZE);
      }
      break;
    case (int)'j': 
      if(cce->optbits & HEXMODE){
	if(cce->hexoffset<(cce->framesize/16-JUMPSIZE))
	  cce->hexoffset+=JUMPSIZE;
	else
	  cce->hexoffset = (cce->framesize/16)-1;
      }
      else{
	err = cce->packet_move(cce->fd, cce->buffer, cce->framesize, JUMPSIZE);
	cce->metadata_increment(cce, JUMPSIZE);
      }
      break;
    case (int)'G': 
      if(cce->optbits & HEXMODE){
	cce->hexoffset = (cce->framesize/16)-1;
      }
      else{
	err = cce->packet_move(cce->fd, cce->buffer, cce->framesize, INT_MAX);
	cce->metadata_increment(cce, INT_MAX);
      }
      break;
    case (int)'g': 
      if(cce->optbits & HEXMODE){
	cce->hexoffset = 0;
      }
      else{
	err = cce->packet_move(cce->fd, cce->buffer, cce->framesize, 0);
	cce->metadata_increment(cce, 0);
      }
      break;
    case (int)'H': 
      //cce->target = cce->mmapfile + cce->framesize*cce->count + cce->offset;
      //fprintf(stdout, " %10X %5X %5X %10X --> ", *((unsigned int*)cce->target), *((short unsigned int*)(cce->target+4)),*((short unsigned int*)(cce->target+6) ) ^ B(15), *((unsigned int*)(cce->target+8)));
      break;
    case 'b':
      cce->optbits ^= HEXMODE;
      cce->hexoffset = 0;
      break;
    case (int)'l': 
      if(cce->optbits & HEXMODE){
	if(cce->hexoffset < (cce->framesize/16 -1 ))
	  cce->hexoffset++;
      }
      else{
	err = cce->packet_move(cce->fd, cce->buffer, cce->framesize, 1);
	cce->metadata_increment(cce, 1);
      }
      break;
  }
  if(err == END_OF_FD){
    LOG("End of file!\n");
    if(cce->reset_to_last_known_good != NULL)
      cce->reset_to_last_known_good(cce);
  }
#ifndef PORTABLE
  err = system ("/bin/stty cooked");
  if(err < 0){
    E("Error in system");
    return -1;
  }
#endif
  return 0;
}
int get_listen_type_from_string(char* match)
{
  if(!(strcmp(match,"tcpstream")))
    return LISTEN_TCPSOCKET;
  else if(!(strcmp(match,"udpstream")))
    return LISTEN_UDPSOCKET;
  else if(!(strcmp(match,"file")))
    return LISTEN_FILE;
  return 0;
}
int get_traverse_type_from_string(char* match)
{
  if(!(strcmp(match,"vim")))
    return TRAVERSE_VIM;
  else if(!(strcmp(match,"check")))
    return TRAVERSE_CHECK;
  else if(!(strcmp(match,"output")))
    return TRAVERSE_OUTPUT;
  return 0;
}
int getopts(int argc, char **argv, struct common_control_element * cce){
  char c;
  while ( (c = getopt(argc, argv, "q:m:t:p:o:w:e:")) != -1) {
    switch (c) {
      case 'q':
	cce->optbits &= ~LOCKER_DATATYPE;
	cce->optbits |= get_datatype_from_string(optarg);
	if((cce->optbits & LOCKER_DATATYPE) == 0){
	  E("No datatype known as %s",, optarg);
	  return -1;
	}
	break;
      case 't':
	cce->optbits &= ~LOCKER_LISTEN;
	cce->optbits |= get_listen_type_from_string(optarg);
	if((cce->optbits & LOCKER_LISTEN) == 0){
	  E("No listen type");
	  return -1;
	}
	break;
      case 'm':
	cce->optbits &= ~LOCKER_TRAVERSE;
	cce->optbits |= get_traverse_type_from_string(optarg);
	if((cce->optbits & LOCKER_TRAVERSE) == 0){
	  E("No traverse type");
	  return -1;
	}
	break;
      case 'p':
	cce->framesize = atoi(optarg);
	if(cce->framesize <= 0|| cce->framesize > MAX_FRAMESIZE){
	  E("Illegal framesize %d",, cce->framesize);
	  return -1;
	}
	break;
      case 'w':
	if(seqnum_preinit_wordsize(cce, atoi(optarg)) != 0)
	  return -1;
      case 'e':
	cce->max_errors = atol(optarg);
	break;
      default:
	E("Unknown parameters %s",, optarg);
	return -1;
    }
  }
  argc -=optind;
  argv += optind;
  if(argc != 1){
    E("Wrong number of non-option arguments. Got %d when expected 1",, argc);
    return -1;
  }
  cce->port_or_filename = argv[0];
  return 0;
}



int main(int argc, char ** argv)
{
  struct common_control_element *cce = malloc(sizeof(struct common_control_element));
  int err;
  memset(cce, 0, sizeof(struct common_control_element));
  cce->max_errors = 30;
  if(getopts(argc, argv, cce) != 0)
  {
    E("Error in getting opts");
    usage(argv[0]);
    exit(-1);
  }
  err = -1;
  switch(cce->optbits & LOCKER_LISTEN)
  {
    case LISTEN_TCPSOCKET:
      err = handle_open_socket(cce);
      break;
    case LISTEN_UDPSOCKET:
      err = handle_open_socket(cce);
      break;
    case LISTEN_FILE:
      err = handle_open_file(cce);
      break;
    default:
      E("No listener set");
      usage(argv[0]);
      exit(-1);
  }
  if(err != 0){
    E("Error in creating listener");
    usage(argv[0]);
    exit(-1);
  }
  err = -1;
  switch(cce->optbits & LOCKER_DATATYPE)
  {
    case(DATATYPE_MARK5B):
      err = init_mark5b_data(cce);
      cce->framesize = MARK5SIZE;
      break;
    case(DATATYPE_MARK5BNET):
      err = init_mark5b_data(cce);
      cce->framesize = MARK5NETSIZE;
      break;
    case(DATATYPE_UDPMON):
      err = init_seqnum_data(cce);
      break;
    case(DATATYPE_VDIF):
      err = init_vdif_data(cce);
      break;
    case(DATATYPE_UNKNOWN):
      E("Unknown datatype");
      usage(argv[0]);
      exit(-1);
  }
  if(err != 0){
    E("Error in setting datatype");
    usage(argv[0]);
    exit(-1);
  }
  /* Why didn't I just stdin?	*/

  cce->buffer = malloc(cce->framesize);

  /* Do first move here	to initialize everything 	*/
  err = cce->packet_move(cce->fd, cce->buffer, cce->framesize, 1);
  CHECK_ERR("Move");

  cce->running = 1;
  while(cce->running == 1)
  {
    switch(cce->optbits & LOCKER_TRAVERSE)
    {
      case(TRAVERSE_OUTPUT):
	err = cce->print_info(cce);
	CHECK_ERR("print info");
	break;
      case(TRAVERSE_VIM):
	err = cce->print_info(cce);
	CHECK_ERR("print after input info");
	err = keyboardinput(cce);
	CHECK_ERR("Keyb input");
	break;
      case(TRAVERSE_CHECK):
	err = cce->check_for_discrepancy(cce);
	CHECK_ERR("checking for discrepancy");
	break;
    }
    if(!(cce->optbits & TRAVERSE_VIM))
    {
      err = cce->packet_move(cce->fd, cce->buffer, cce->framesize, 1);
      if(err == END_OF_FD)
      {
	LOG("Clean exit on reader\n");
	cce->running = 0;
      }
      else if(err < 0)
      {
	E("Error in packet move");
	cce->running = 0;
      }
      cce->metadata_increment(cce, 1);
    }
    if(cce->errors > cce->max_errors){
      LOG("Max errors in stream reached. Exiting\n");
      cce->running = 0;
    }
  }

  cce->cleanup_reader(cce);
  cce->cleanup_inspector(cce);
  free(cce->buffer);
  free(cce);
  exit(0);
}
