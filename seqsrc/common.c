#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>

#include "common.h"


unsigned int get_mask(int start, int end){
  unsigned int returnable = 0;
  while(start <= end){
    returnable |= B(start);
    start++;
  }
  return returnable;
}

int keyboardinput(struct common_control_element * cce){
  char dachar;
  int err;
#ifndef PORTABLE
  err = system ("/bin/stty raw");
  if(err < 0){
    O("Error in system");
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
	if(cce->count>0)
	  cce->count--;
      }
      break;
    case (int)'k': 
      if(cce->optbits & HEXMODE){
	if(cce->hexoffset>JUMPSIZE)
	  cce->hexoffset-=JUMPSIZE;
      }
      else{
	if(cce->count>JUMPSIZE)
	  cce->count-=JUMPSIZE;
	else
	  cce->count = 0;
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
	if((cce->count) <(cce->fsize/cce->framesize - JUMPSIZE))
	  cce->count+=JUMPSIZE;
	else
	  cce->count = cce->fsize/cce->framesize -1;
      }
      break;
    case (int)'G': 
      if(cce->optbits & HEXMODE){
	cce->hexoffset = (cce->framesize/16)-1;
      }
      else{
	cce->count = cce->fsize/cce->framesize -1;
      }
      break;
    case (int)'g': 
      if(cce->optbits & HEXMODE){
	cce->hexoffset = 0;
      }
      else{
	cce->count = 0;
      }
      break;
    case (int)'H': 
      cce->target = cce->mmapfile + cce->framesize*cce->count + cce->offset;
      fprintf(stdout, " %10X %5X %5X %10X --> ", *((unsigned int*)cce->target), *((short unsigned int*)(cce->target+4)),*((short unsigned int*)(cce->target+6) ) ^ B(15), *((unsigned int*)(cce->target+8)));
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
	if((cce->count) <(cce->fsize/cce->framesize - 1))
	  cce->count++;
      }
      break;
  }
#ifndef PORTABLE
  err = system ("/bin/stty cooked");
  if(err < 0){
    O("Error in system");
    return -1;
  }
#endif
  return 0;
}
int getopts(int argc, char **argv, struct common_control_element * cce){
  char c;
  while ( (c = getopt(argc, argv, "anf:s")) != -1) {
    //int this_option_optind = optind ? optind : 1;
    switch (c) {
      case 'n':
	cce->framesize = MARK5NETSIZE;
	cce->offset = MARK5OFFSET;
	cce->optbits |= NETMODE;
	break;
      case 'f':
	if(stat(optarg, &(cce->st)) != 0){
	  O("error in stat\n");
	  return -1;
	}
	cce->fd = open(optarg, O_RDONLY);
	if(cce->fd == -1){
	  O("Error opening file %s\n", optarg);
	  return -1;
	}
	break;
      case 'a':
	cce->optbits |= ISAUTO;
	break;
      case 's':
	//seek = 1;
	cce->optbits |= SEEKIT;
      default:
	O("Unknown parameters %s\n", optarg);
	return -1;
    }
  }
  return 0;
}
