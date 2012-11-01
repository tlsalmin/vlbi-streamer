#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/mman.h>
//#include "common.h"
#include "mark5b.h"
#define GRAB_4_BYTES \
  memcpy(&read_count, target, 4);\
  target+=4;
  //read_count = be32toh(read_count);

  /*
  if(read(fd, &read_count,4) < 0){ \
    O("Read error!\n"); \
    break; \
  }\
read_count = be32toh(read_count);
*/
void usage(){
  O("Usage: check_mark5b -f <file> (-n if networked packets)\n");
  exit(-1);
}
//#define MULPLYTEN
int main(int argc, char ** argv){
  int fd,err;
  long count = 0;
  long fsize;
  int dachar;
  void* mmapfile = NULL;
  void* target;
  int read_count;
  int pagesize = getpagesize();
  struct stat st;
  int running = 1;
  int offset = 0;
  int framesize = MARK5SIZE;

  int syncword;
  int userspecified;
  int tvg;
  int framenum;
  int VLBABCD_timecodeword1J;
  int VLBABCD_timecodeword1S;
  int VLBABCD_timecodeword2;
  int CRCC;
  char c;
  int isauto=0;

  while ( (c = getopt(argc, argv, "anf:")) != -1) {
        //int this_option_optind = optind ? optind : 1;
        switch (c) {
	  case 'n':
	    framesize = MARK5NETSIZE;
	    offset = MARK5OFFSET;
	    break;
	  case 'f':
	    if(stat(optarg, &st) != 0){
	      O("error in stat\n");
	      exit(-1);
	    }
	    fd = open(optarg, O_RDONLY);
	    if(fd == -1){
	      O("Error opening file %s\n", optarg);
	      exit(-1);
	    }
	    break;
	  case 'a':
	    isauto=1;
	    break;


	  default:
	    usage();
	    break;
	}
  }
  /*
     O("argc %d\n", argc);
     argv +=optind;
     argc -=optind;


     if(argc < 2){
     O("Usage: %s <filename> \n", argv[0]);
     exit(-1);
     }
     */


  fsize = st.st_size;
  if(fsize % pagesize != 0){
    long temp = fsize;
    while(fsize % pagesize != 0){
      fsize--;
    }
    O("Had to remove %ld from fsize for alignment\n", temp-fsize); 
  }

  mmapfile = mmap(NULL, fsize, PROT_READ, MAP_PRIVATE, fd, 0);
  if(((long)mmapfile) == -1)
    perror("mmap file");
  while(running){

    target = mmapfile + framesize*count + offset;
    GRAB_4_BYTES
      syncword = read_count;

    GRAB_4_BYTES
      userspecified = (read_count & get_mask(16,31)) >> 16;
    tvg = (read_count & B(15)) >> 15;
    framenum = read_count & get_mask(0,14);

    GRAB_4_BYTES
#ifdef MULPLYTEN
      //read_count = be32toh(read_count);
      VLBABCD_timecodeword1J = m5getMJD(read_count);
    VLBABCD_timecodeword1S= m5getsecs((unsigned int)read_count);
    //VLBABCD_timecodeword1S = read_count & get_mask(0,20);
#else
    VLBABCD_timecodeword1J = (read_count & get_mask(20,31)) >> 20;
    VLBABCD_timecodeword1S = read_count & get_mask(0,20);
#endif
    GRAB_4_BYTES
#ifdef MULPLYTEN
    VLBABCD_timecodeword2 = m5getmyysecs((unsigned int)read_count);
#else
    VLBABCD_timecodeword2 = (((unsigned int)read_count) & get_mask(16,31)) >> 16;
#endif
    CRCC = read_count & get_mask(0,15);

    //fprintf(stdout, "---------------------------------------------------------------------------\n");
#ifdef MULPLYTEN
    fprintf(stdout, "syncword: %5X | userspecified: %6X | tvg: %6s | framenum: %6d | timecodeword1J: %6d mjd| timecordword1S: %6d s | timecodeword2: %6d myys | CRCC: %6d\n",syncword, userspecified, BOLPRINT(tvg), framenum, VLBABCD_timecodeword1J, VLBABCD_timecodeword1S, VLBABCD_timecodeword2, CRCC);
#else
    fprintf(stdout, "syncword: %5X | userspecified: %6X | tvg: %6s | framenum: %6d | timecodeword1J: %6X mjd| timecordword1S: %6X s | timecodeword2: 0.%6X s | CRCC: %6d\n",syncword, userspecified, BOLPRINT(tvg), framenum, VLBABCD_timecodeword1J, VLBABCD_timecodeword1S, VLBABCD_timecodeword2, CRCC);
#endif
    //fprintf(stdout, "| vdif_version: %2d | log2_channels: %4d | frame_length %14d |\n", vdif_version, log2_channels, frame_length);
    //fprintf(stdout, "| data_type: %8s | bits_per_sample: %6d | thread_id: %6d | station_id %8d |\n", DATATYPEPRINT(data_type), bits_per_sample, thread_id, station_id);

    if(isauto == 0){
#ifndef PORTABLE
    system ("/bin/stty raw");
#endif
    dachar = getchar();
    switch(dachar)
    {
      case (int)'q': 
	running = 0;
	break;
      case (int)'h': 
	if(count>0)
	  count--;
	break;
      case (int)'k': 
	if(count>JUMPSIZE)
	  count-=JUMPSIZE;
	else
	  count = 0;
	break;
      case (int)'j': 
	if((count) <(fsize/framesize - JUMPSIZE))
	  count+=JUMPSIZE;
	else
	  count = fsize/framesize -1;
	break;
      case (int)'G': 
	count = fsize/framesize -1;
	break;
      case (int)'g': 
	count = 0;
	break;
      case (int)'l': 
	if((count) <(fsize/framesize - 1))
	  count++;
	break;
    }
#ifndef PORTABLE
    system ("/bin/stty cooked");
#endif
    }
    else{
      count++;
      if (count == fsize/framesize -1)
	running = 0;

    }
  }

  err = munmap(mmapfile, fsize);
  if(err != 0)
    perror("unmap\n");

  if(close(fd) != 0){
    O("Error on close\n");
    exit(-1);
  }

  exit(0);
}
