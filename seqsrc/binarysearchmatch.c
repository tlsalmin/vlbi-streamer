
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
#define MAXMATCH 1024

#define DEFAULT_CORES 6
#define MINMEM 2l


int framesize;
int offset;
long filesizem;
int extraiterations;
long filesizes;
int keepgoing;
int maxmatch;
long read_num_packets_per_iteration;

void usage(){
  O("Usage: binarysearch -m <file1> -s <file2> -t <type> (mark5b, mark5bnet, vdif) -c <cores> -b(do be32toh to master), -a <maxmatching bytes> -b (flip endian on matcher)");
  exit(-1);
}
int read_file_to_mem(long no_packets,  int fd, void* mempoint){
  void* tempframe = malloc(framesize);
  int err;
  long i;
  for(i=0;i<no_packets;i++)
  {
    err = read(fd, tempframe, framesize);
    if(err <0){
      O("Error in red");
      return -1;
    }
    memcpy(mempoint+i*PAYLOAD, tempframe+offset, PAYLOAD);
  }
  return 0;
}
int match_for_whole_file(void* needle_prim, int fd,long packets, void* haystack){
  long j;
  int err;
  int running;
  long reallyread;
  int addtoneedle=0;
  int matchlength = 0;
  void* needle = NULL;
  int bytelengthofmatch;
  for(j=0;j<packets;j+=read_num_packets_per_iteration){
    bytelengthofmatch = 4;
    if(read_num_packets_per_iteration >= packets)
      reallyread = packets;
    else
      reallyread = MIN(read_num_packets_per_iteration, packets-read_num_packets_per_iteration);

    O("Reading %ld packets to mem\n", reallyread);
    err =  read_file_to_mem(reallyread, fd, haystack);
    if(err != 0){
      O("Err in read!");
      return -1;
    }

    /*
    O("Preview of first line of haystack:\n");
    O("%x\t%x\t%x\t%x\n", *((int32_t*)haystack), *((int32_t*)(haystack+4)), *((int32_t*)(haystack+8)), *((int32_t*)(haystack+12)));
    */

    running = 1;
    //uint32_t temppi;
    int reported=0;
    O("Loaded iteration for up to packet %ld. Starting match searching\n", j);
    while(running){
      needle = memmem(haystack, (reallyread*(framesize-offset)), needle_prim+addtoneedle, bytelengthofmatch);
      if (needle != NULL){
	long target = ((long)needle-(long)haystack)/(framesize-offset);
	target = (framesize*j + target*framesize)/MEG;
	if(reported == 0){
	  O("Found %d length needle from slave in haystack  master at byte offset %ldMB!\n", bytelengthofmatch, target);
	  reported =1;
	}
	if(bytelengthofmatch > matchlength)
	  matchlength = bytelengthofmatch;
	bytelengthofmatch += 4;
	if(bytelengthofmatch > (maxmatch-addtoneedle)){
	  running=0;
	  O("Found %d length needle from slave in haystack  master at byte offset %ldMB!\n", bytelengthofmatch, target);
	  //TODO: Eww..
	  if(keepgoing==0){
	    keepgoing= -1;
	    break;
	  }
	}
      }
      else{
	if(reported == 1){
	  O("Matching stopped after %d\n", bytelengthofmatch);
	  reported =0;
	}
	if(extraiterations){
	  if(addtoneedle == 0)
	  //if(addtoneedle < maxmatch-4)
	  {
	    addtoneedle+=maxmatch/4;
	    bytelengthofmatch = 4;
	  }
	  else
	    running = 0;
	}
	else{
	  O("Abandon hope at %d length match\n", bytelengthofmatch);
	  running=0;
	}
      }
    }
    /* We wouldn't want a match to break if its on the edge of a packet */
    /* So we wind back one packet */

    if(keepgoing==-1){
      keepgoing=0;
      break;
    }


    j-=1;
    lseek(fd, -framesize, SEEK_CUR);

    /*
    bytelengthofmatch = 4;
    running = 1;
    O("Reading master file to mem\n");
    err =  read_file_to_mem(read_num_packets_per_iteration, fdm, filem);
    if(err != 0)
      exit(-1);
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
    */
  }
  return matchlength;
}

int main(int argc, char ** argv){
  int err, type;
  int i;
  //void* needle;
  extraiterations = 0;
  void* realneedlem;
  void* realneedles;
  keepgoing=0;
  void *filem, *files;
  long packetsm,packetss;
  read_num_packets_per_iteration=0;
  filesizem=0;
  filesizes=0;
  /*
  int offset = 0;
  int framesize=0;
  */
  maxmatch = MAXMATCH;
  char c;
  int fds, fdm;
  type = TYPE_MARK5B;
  offset = MARK5HEADER;
  framesize = MARK5SIZE;
  struct stat st;
  int beit = 0;


  while ( (c = getopt(argc, argv, "t:m:s:ba:x")) != -1) {
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
      case 'x':
	extraiterations =1;
	break;
      case 'k':
	keepgoing=1;
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

  read_num_packets_per_iteration = GIG*MINMEM/((long)framesize-(long)offset);
  /*
  while((read_num_packets_per_iteration*(framesize-offset)) < GIG*MINMEM)
    read_num_packets_per_iteration = read_num_packets_per_iteration << 1;
    */

  O("Going to mount files in %ldMB chunks\n", (read_num_packets_per_iteration*(framesize-offset))/MEG);

  (void)type;
  packetsm = filesizem/framesize;
  packetss = filesizes/framesize;

  //filem = malloc(filesizem - offset*packetsm);
  filem = malloc(read_num_packets_per_iteration*(framesize-offset));
  if(filem ==NULL){
    O("Cant malloc %ldMB for master file \n", (read_num_packets_per_iteration*(framesize-offset))/MEG);
    exit(-1);
  }
  //files = malloc(filesizes - offset*packetss);
  files = malloc(read_num_packets_per_iteration*(framesize-offset));
  if(files ==NULL){
    O("Cant malloc %ldMB for slave file \n", (read_num_packets_per_iteration*(framesize-offset))/MEG);
    exit(-1);
  }

  realneedles= malloc(maxmatch);
  realneedlem= malloc(maxmatch);

  void* tempframe = malloc(framesize);
  O("Reading needles\n");
  err = read(fdm, tempframe, framesize);
  if(err < 0){
    O("Error in read");
    exit(-1);
  }
  memcpy(realneedlem, tempframe+offset, maxmatch);
  err = read(fds, tempframe, framesize);
  if(err < 0){
    O("Error in read");
    exit(-1);
  }
  memcpy(realneedles, tempframe+offset, maxmatch);

  lseek(fdm, 0, SEEK_SET);
  lseek(fds, 0, SEEK_SET);



  free(tempframe);
  //memcpy(realneedlem, filem, maxmatch);
  //memcpy(realneedles, files, maxmatch);
  //realneedlem = filem;
  //realneedles = files;
  if(beit == 1){
    for(i=0;i*4<maxmatch;i++){
      //temppi = *((uint32_t*)(files+i*4));

      *((uint32_t*)(realneedlem+i*4)) = be32toh(*((uint32_t*)(realneedlem+i*4)));
      *((uint32_t*)(realneedles+i*4)) = be32toh(*((uint32_t*)(realneedles+i*4)));
    }
  }

  O("Masters needle is \t");
  O("%x\t%x\t%x\t%x\n", *((int32_t*)realneedlem), *((int32_t*)(realneedlem+4)), *((int32_t*)(realneedlem+8)), *((int32_t*)(realneedlem+12)));
  O("Slaves needle is \t");
  O("%x\t%x\t%x\t%x\n", *((int32_t*)realneedles), *((int32_t*)(realneedles+4)), *((int32_t*)(realneedles+8)), *((int32_t*)(realneedles+12)));

  err = match_for_whole_file(realneedlem, fds, packetss, files);
  if(err < 0){
    O("Error in match for fds");
  }
  else{
    O("Longest match for master in slave is %d bytes\n", err);
  }
  err = match_for_whole_file(realneedles, fdm, packetsm,filem);
  if(err < 0){
    O("Error in match for fds");
  }
  else{
    O("Longest match for slave in master is %d bytes\n", err);
  }

  //free(iov);
  free(realneedles);
  free(realneedlem);

  close(fds);
  close(fdm);

  free(filem);
  free(files);
  exit(0);
}
