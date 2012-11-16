
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <limits.h>
#include <stdint.h>

#include "common.h"
#include "mark5b.h"

#define PAYLOAD (framesize-offset)
#define MAXMATCH 512

#define MIN(x,y) (x < y ? x : y)
#define DEFAULT_CORES 6

int framesize;
int offset;

void usage(){
  O("Usage: binarysearch -m <file1> -s <file2> -t <type> (mark5b, mark5bnet, vdif) -c <cores> -b(do be32toh to master), -a <maxmatching bytes> -b (flip endian on matcher)");
  exit(-1);
}
int read_file_to_mem(long no_packets,  int fd, void* mempoint){
  void* tempframe = malloc(framesize);
  long i;
  for(i=0;i<no_packets;i++)
  {
    read(fd, tempframe, framesize);
    memcpy(mempoint+i*PAYLOAD, tempframe+offset, PAYLOAD);
  }
  return 0;
}

int main(int argc, char ** argv){
  int err, type;
  long filesizem=0;
  long filesizes=0;
  long packetsm,packetss;
  int i;
  void* needle;
  int bytelengthofmatch = 4;
  /*
  int offset = 0;
  int framesize=0;
  */
  void *filem, *files;
  int maxmatch = MAXMATCH;
  char c;
  int fds, fdm;
  struct stat st;
  int beit = 0;


  while ( (c = getopt(argc, argv, "t:m:s:ba:")) != -1) {
    //int this_option_optind = optind ? optind : 1;
    switch (c) {
      case 'a':
	maxmatch = atoi(optarg);
	break;
      case 't':
	if (!strcmp(optarg, "mark5b")){
	  //opt->capture_type = CAPTURE_W_FANOUT;
	  type = TYPE_MARK5B;
	  offset = MARK5HEADER;
	  framesize=MARK5SIZE;
	}
	else if (!strcmp(optarg, "mark5bnet")){
	  //opt->capture_type = CAPTURE_W_UDPSTREAM;
	  type= TYPE_MARK5BNET;
	}
	else if (!strcmp(optarg, "vdif")){
	  //opt->capture_type = CAPTURE_W_SPLICER;
	  type = TYPE_VDIF;
	}
	else {
	  O("Unknown file type [%s]\n", optarg);
	  usage();
	  //return -1;
	}
	O("Type is %s\n", optarg);
	break;
      case 'b':
	beit = 1;
	break;
      case 'm':
	if(stat(optarg, &st) != 0){
	  O("error in stat\n");
	  exit(-1);
	}
	filesizem=st.st_size;
	fdm = open(optarg, O_RDONLY);
	if(fdm == -1){
	  O("Error opening file %s\n", optarg);
	  exit(-1);
	}
	break;
      case 's':
	if(stat(optarg, &st) != 0){
	  O("error in stat\n");
	  exit(-1);
	}
	filesizes=st.st_size;
	fds = open(optarg, O_RDONLY);
	if(fds == -1){
	  O("Error opening file %s\n", optarg);
	  exit(-1);
	}
	break;
      //case 'c':
	//cores = atoi(optarg);
	break;
      default:
	usage();
	break;
    }
  }
  if(filesizem == 0 || filesizes == 0){
    O("missing files\n");
    usage();
  }
  (void)type;
  packetsm = filesizem/framesize;
  packetss = filesizes/framesize;

  filem = malloc(filesizem-offset*packetsm);
  files = malloc(filesizes- offset * packetss);


  O("Reading master file to mem\n");
  err =  read_file_to_mem(packetsm, fdm, filem);
  if(err != 0)
    exit(-1);
  O("Reading slave file to mem\n");
  err =  read_file_to_mem(packetss, fds, files);
  if(err != 0)
    exit(-1);
  O("Done reading files to mem\n");
  
  O("Preview of first line of master:\n");
  O("%x\t%x\t%x\t%x\n", *((int32_t*)filem), *((int32_t*)(filem+4)), *((int32_t*)(filem+8)), *((int32_t*)(filem+12)));
  O("Preview of first line of slave:\n");
  O("%x\t%x\t%x\t%x\n", *((int32_t*)files), *((int32_t*)(files+4)), *((int32_t*)(files+8)), *((int32_t*)(files+12)));

  int running = 1;
  void* realneedle;
  //uint32_t temppi;
  if(beit == 0){
    realneedle = files;
  }
  else{
    realneedle= malloc(maxmatch);
    for(i=0;i*4<maxmatch;i++){
      //temppi = *((uint32_t*)(files+i*4));
      
      *((uint32_t*)(realneedle+i*4)) = be32toh(*((uint32_t*)(files+i*4)));
    }
  }
  while(running){
    needle = memmem(filem, filesizem-offset*packetsm, realneedle, bytelengthofmatch);
    if (needle != NULL){
      O("Found %d length needle from slave in haystack  master at %ld!\n", bytelengthofmatch, (needle-filem));
      bytelengthofmatch += 4;
      if(bytelengthofmatch > maxmatch)
	running=0;
    }
    else
      running=0;

  }
  bytelengthofmatch = 4;
  if(beit == 0){
    realneedle = filem;
  }
  else{
    realneedle= malloc(maxmatch);
    for(i=0;i*4<maxmatch;i++){
      //temppi = *((uint32_t*)(files+i*4));
      
      *((uint32_t*)(realneedle+i*4)) = be32toh(*((uint32_t*)(filem+i*4)));
    }
  }
  running = 1;
  while(running){
    needle = memmem(files, filesizes-offset*packetss, realneedle, bytelengthofmatch);
    if (needle != NULL){
      O("Found %d length needle from master in haystack of slave at %ld!\n", bytelengthofmatch, (needle-files));
      bytelengthofmatch += 4;
      if(bytelengthofmatch > maxmatch)
	running=0;
    }
    else
      running=0;

  }

  //free(iov);
  if(beit == 1)
    free(realneedle);

  close(fds);
  close(fdm);

  free(filem);
  free(files);
  exit(0);
}
