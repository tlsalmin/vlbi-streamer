#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>

#include "commonffu.h"

#ifdef GRAB_4_BYTES
#undef GRAB_4_BYTES
#endif
#define GRAB_4_BYTES \
  memcpy(&read_count, target, 4);\
  target+=4;\
  read_count = be32toh(read_count);

void usage(){
  O("Usage: check_vdif_seq -f <file> (-s vdifsize(default 8224))(-a for no navigation, just spam))\n");
  exit(-1);
}

int main(int argc, char** argv){
  int fd,err;
  long count = 0;
  long fsize;
  int offset = 0;
  long framesize;
  char dachar;
  int pagesize = getpagesize();
  void * mmapfile = NULL;
  void * target;
  unsigned int read_count;
  struct stat st;
  char c;
  int isauto=0;
  int running = 1;
  framesize = VDIFSIZE;
  while ( (c = getopt(argc, argv, "as:f:")) != -1) {
        //int this_option_optind = optind ? optind : 1;
        switch (c) {
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
	  case 's':
	    framesize = atoi(optarg);
	    break;
	  default:
	    usage();
	    break;
	}
  }
  
  //framesize = atol(argv[2]);

  fsize = st.st_size;

  //fsize = st.st_size;
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
  /*
  if(read(fd, &count,8) < 0){
    O("Read error!");
    exit(-1);
  }
  count = be64toh(count);
  O("first count is %ld\n", count);
  count++;
  */
  int valid_data, legacy_mode, seconds_from_epoch, epoch, data_frame_num, vdif_version, log2_channels, frame_length, data_type, bits_per_sample, thread_id, station_id;

  //for(i=0;i*framesize < fsize;i++){
  while(running)
  {
    target = mmapfile + framesize*count + offset;

    //lseek(fd, i*framesize, SEEK_SET);

    GRAB_4_BYTES
    valid_data = read_count & B(31) << 31;
    legacy_mode = read_count & B(30) << 30;
    seconds_from_epoch = read_count & get_mask(0,29);
      /*
    valid_data = 0x80000000 & read_count;
    legacy_mode = 0x40000000 & read_count;
    seconds_from_epoch = 0x3fffffff & read_count;
    */

    GRAB_4_BYTES
    epoch = read_count & get_mask(24,29) << 24;
    data_frame_num = read_count & get_mask(0,23);
      /*
    epoch = 0x3e000000 & read_count;
    data_frame_num = 0x01ffffff & read_count;
    */

    GRAB_4_BYTES
    vdif_version = read_count & get_mask(29,31) << 29;
    log2_channels = read_count & get_mask(24,28) << 24;
    frame_length = read_count & get_mask(0,23);
      /*
    vdif_version = 0xe0000000 & read_count;
    log2_channels = 0x1f000000 & read_count;
    frame_length = 0x00ffffff & read_count;
    */

    GRAB_4_BYTES
    data_type = read_count & B(31) << 31;
    bits_per_sample = read_count & get_mask(26,30) << 26;
    thread_id = read_count & get_mask(16,25) << 26;
    station_id = read_count & get_mask(0,15);
      /*
    data_type = 0x80000000 & read_count;
    bits_per_sample = 0x7c000000 & read_count;
    thread_id = 0x03ff0000 & read_count;
    station_id = 0x0000ffff & read_count;
    */

    fprintf(stdout, "---------------------------------------------------------------------------\n");
    fprintf(stdout, "| valid_data: %5s | legacy_mode: %5s | seconds_from_epoch: %14d |\n", BOLPRINT(valid_data), BOLPRINT(legacy_mode), seconds_from_epoch);
    fprintf(stdout, "| epoch: %14d | data_frame_num: %14d |\n", epoch, data_frame_num);
    fprintf(stdout, "| vdif_version: %2d | log2_channels: %4d | frame_length %14d |\n", vdif_version, log2_channels, frame_length);
    fprintf(stdout, "| data_type: %8s | bits_per_sample: %6d | thread_id: %6d | station_id %8d |\n", DATATYPEPRINT(data_type), bits_per_sample, thread_id, station_id);

    
    /*
    if(count != read_count){
      fprintf(stdout, "Discrepancy as count is %ld and read_count is %ld\n",count, read_count);
      //count = read_count;
    }
    */
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
  fprintf(stdout, "Done!\n");

  err = munmap(mmapfile, fsize);
  if(err != 0)
    perror("unmap\n");

  if(close(fd) != 0){
    O("Error on close\n");
    exit(-1);
  }

  exit(0);
}
