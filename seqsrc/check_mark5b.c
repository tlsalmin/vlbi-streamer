#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/mman.h>
#include "common.h"
//#include "mark5b.h"
//#define BEITONNET
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
  O("-a to spew out everything in automode");
  exit(-1);
}
//#define MULPLYTEN
int main(int argc, char ** argv){
  int fd=-1,err;
  //struct common_control_element * cce = (struct common_control_element*)malloc(sizeof(common_control_element));
//  count = 0;
  long count = 0;
  long fsize;
  int dachar;
  void* mmapfile = NULL;
  void* target;
  unsigned int read_count;
  int pagesize = getpagesize();
  struct stat st;
  int running = 1;
  int offset = 0;
  int framesize = MARK5SIZE;
  int hexmode=0;

  int syncword;
  int userspecified;
  int hexoffset=0;
  int extraoffset = 0;
  int tvg;
  int framenum=0;
  int VLBABCD_timecodeword1J;
  int VLBABCD_timecodeword1S =0;
  int VLBABCD_timecodeword2;
  //int seek = 0;
  int CRCC;
  char c;
  int isauto=0;
  int netmode=0;

  while ( (c = getopt(argc, argv, "anf:o:")) != -1) {
        //int this_option_optind = optind ? optind : 1;
        switch (c) {
	  case 'n':
	    framesize = MARK5NETSIZE;
	    offset = MARK5OFFSET;
	    netmode=1;
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
	  case 'o':
	    extraoffset=atoi(optarg);
	    break;
	  //case 's':
	    //seek = 1;
	  default:
	    usage();
	    break;
	}
  }
  if(extraoffset!=0)
    framesize+=extraoffset;
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

  /* Check if we started at half frame */
  target = mmapfile+offset+extraoffset;
  GRAB_4_BYTES
  if(read_count != 0xABADDEED){
    if(netmode == 0)
    {
    O("Adding 5008 offset, since recording started at midway of a mark5b frame");
    offset+=5008;
    }
    else{
    O("Adding 5016 offset, since recording started at midway of a mark5b frame");
    offset+=5016;
    }
  }
  while(running){

    /*
    if(hexmode && netmode){
      target = mmapfile + framesize*count + 5016 + hexoffset*16;
    }
    else
    */
    target = mmapfile + framesize*count + offset + hexoffset*16 +extraoffset;
    if(hexmode == 0){
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
    VLBABCD_timecodeword1S = read_count & get_mask(0,19);
    //O("%X\n", VLBABCD_timecodeword1S);
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
    }
    else{
#ifdef BEITONNET
      if(netmode)
	fprintf(stdout, "\t%X ", be32toh(*((int32_t*)target)));
      else
#endif
	fprintf(stdout, "\t%X ", *((int32_t*)target));
      target+=4;
#ifdef BEITONNET
      if(netmode)
	fprintf(stdout, "\t%X ", be32toh(*((int32_t*)target)));
      else
#endif
	fprintf(stdout, "\t%X ", *((int32_t*)target));
      target+=4;
#ifdef BEITONNET
      if(netmode)
	fprintf(stdout, "\t%X ", be32toh(*((int32_t*)target)));
      else
#endif
	fprintf(stdout, "\t%X ", *((int32_t*)target));
      target+=4;
#ifdef BEITONNET
      if(netmode)
	fprintf(stdout, "\t%X \n", be32toh(*((int32_t*)target)));
      else
#endif
	fprintf(stdout, "\t%X \n", *((int32_t*)target));
      //fprintf(stdout, "\t%X %X %X %X\n", *((int16_t*)target), *((int16_t*)(target+4)), *((int16_t*)(target+8)), *((int16_t*)(target+12)));
    }
    //fprintf(stdout, "| vdif_version: %2d | log2_channels: %4d | frame_length %14d |\n", vdif_version, log2_channels, frame_length);
    //fprintf(stdout, "| data_type: %8s | bits_per_sample: %6d | thread_id: %6d | station_id %8d |\n", DATATYPEPRINT(data_type), bits_per_sample, thread_id, station_id);

    if(isauto == 0){
#ifndef PORTABLE
      err = system ("/bin/stty raw");
      if(err < 0){
	O("Err in system");
	exit(-1);
      }
#endif
      dachar = getchar();
      switch(dachar)
      {
	case (int)'q': 
	running = 0;
	break;
      case (int)'h': 
	if(hexmode){
	  if(hexoffset>0)
	    hexoffset--;
	}
	else{
	if(count>0)
	  count--;
	}
	break;
      case (int)'k': 
	if(hexmode){
	  if(hexoffset>JUMPSIZE)
	    hexoffset-=JUMPSIZE;
	}
	else{
	if(count>JUMPSIZE)
	  count-=JUMPSIZE;
	else
	  count = 0;
	}
	break;
      case (int)'j': 
	if(hexmode){
	  if(hexoffset<(framesize/16-JUMPSIZE))
	    hexoffset+=JUMPSIZE;
	  else
	    hexoffset = (framesize/16)-1;
	}
	else{
	if((count) <(fsize/framesize - JUMPSIZE))
	  count+=JUMPSIZE;
	else
	  count = fsize/framesize -1;
	}
	break;
      case (int)'G': 
	if(hexmode){
	  hexoffset = (framesize/16)-1;
	}
	else{
	count = fsize/framesize -1;
	}
	break;
      case (int)'g': 
	if(hexmode){
	  hexoffset = 0;
	}
	else{
	count = 0;
	}
	break;
      case (int)'s':
#ifndef PORTABLE
    err = system ("/bin/stty cooked");
      if(err < 0){
	O("Err in system");
	exit(-1);
      }
#endif
	char* tempstring = (char*)malloc(sizeof(char)*FILENAME_MAX);
	char* temp = NULL;
	int seconds;
	int tempfd;
	long end;
	int timecode2;
	//long last;
	int framenum2;
	long framesinsecond;
	/* VLBABCD_timecodeword1S  is our start time */

	fprintf(stdout, "Seconds:");
	temp = fgets(tempstring, FILENAME_MAX, stdin);
	if(temp == NULL){
	  O("Error in getting seconds");
	  free(tempstring);
	  break;
	}
	temp = NULL;
	seconds = atoi(tempstring);

	fprintf(stdout, "Outputfile:");
	temp = fgets(tempstring, FILENAME_MAX, stdin);
	if(temp == NULL){
	  O("Error in getting seconds");
	  free(tempstring);
	  break;
	}

	tempfd = open(tempstring, O_WRONLY|O_CREAT|S_IWUSR,S_IRUSR|S_IWGRP|S_IRGRP);
	if(fd == -1){
	  O("Error opening file %s\n", tempstring);
	  break;
	  //exit(-1);
	}

	/* TODO: Refactor to function blabla */
	timecode2 = VLBABCD_timecodeword1S;
	end = count;
	framenum2 = framenum;
	//while(timecode2 == VLBABCD_timecodeword1S){
	while(framenum2 != 0){
	  target = mmapfile + framesize*end + offset + hexoffset*16;
	  GRAB_4_BYTES
	  GRAB_4_BYTES
	  framenum2 = read_count & get_mask(0,14);
	  GRAB_4_BYTES
	  timecode2 = read_count & get_mask(0,19);
	  end++;
	}
	O("Got to next second: %X, frame %d", timecode2, framenum2);
	target = mmapfile + framesize*(end-2) + offset + hexoffset*16;
	GRAB_4_BYTES
	GRAB_4_BYTES
	framesinsecond = (read_count & get_mask(0,14)) +1;
	GRAB_4_BYTES
	timecode2 = read_count & get_mask(0,19);

	target = mmapfile + framesize*count + offset + hexoffset*16;
	O("Writing %i seconds, with %ld frames in a second with %i framesize\n", seconds, framesinsecond, framesize);

	err = write(tempfd,target, seconds*framesinsecond*framesize);
	if(err <0)
	  O("Error in write\n");

	close(tempfd);
	free(tempstring);
	break;
      case (int)'H': 
	target = mmapfile + framesize*count + offset;
	fprintf(stdout, " %10X %5X %5X %10X --> ", *((unsigned int*)target), *((short unsigned int*)(target+4)),*((short unsigned int*)(target+6) ) ^ B(15), *((unsigned int*)(target+8)));
	break;
      case 'b':
	hexmode ^= 1;
	hexoffset = 0;
	break;
      case (int)'l': 
	if(hexmode){
	  if(hexoffset < (framesize/16 -1 ))
	    hexoffset++;
	}
	else{
	if((count) <(fsize/framesize - 1))
	  count++;
	}
	break;
    }
#ifndef PORTABLE
    err = system ("/bin/stty cooked");
      if(err < 0){
	O("Err in system");
	exit(-1);
      }
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
