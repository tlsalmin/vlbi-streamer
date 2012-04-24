#include <stdio.h>
#include <stdlib.h>
int main(int argc, char** argv){
  unsigned long i = 1500;
  unsigned long minmem = (4l)*(1<<30);
  unsigned long maxmem = (12l)*(1<<30);
  while(i<60000){
    unsigned long bufsize;
    unsigned long temp = minmem/i;
    bufsize = temp*i;
    while(bufsize<minmem)
      bufsize+=i;
    unsigned long found_512 = 0;
    while(bufsize <maxmem){
      if(bufsize % 512 == 0){
	found_512 = 1;
	break;
      }
      bufsize+= i;
    }
    if(found_512 == 0){
      fprintf(stdout, "Didnt find alignment for %lu\n", i);
    }
    else{
      //fprintf(stdout, "Alignment found fo %lu at %lu\n", i, bufsize);
    }
    i++;
  }
  unsigned long magic = 8;
  unsigned long j;
  unsigned long hdwritemin;
  //for(magic=2;magic<1024;magic*=2){
  fprintf(stdout, "#Packetsize\ttreads\tw_every\n");
  for(hdwritemin=1<<16;hdwritemin<33554432+1;hdwritemin = hdwritemin << 1){
    fprintf(stdout, "#Min write size %lu\n", hdwritemin);
    for(magic=2;magic<128;magic++){
      for(j=1;j<33;j++){
	for(i=1500;i<60000;i++){
	  unsigned long found = 0;
	  unsigned long bufsize = i;
	  /*
	     while(bufsize*magic*j < minmem)
	     bufsize+=i;
	     */
	  unsigned long temp = hdwritemin/i;
	  bufsize = temp*i;
	  while(bufsize*magic*j < maxmem){
	    if(bufsize % 512 == 0 && bufsize % magic == 0){
	      found=1;
	      break;
	    }
	    bufsize+=i;
	  }
	  if(found ==0){
	    //fprintf(stdout, "Didnt find alignment for %lu on %lu threads, with w_every %lu, with hdwritemin %lu\n", i,j, magic, hdwritemin);
	    fprintf(stdout, "%lu\t%lu\t%lu\n", i,j,magic);
	    break;
	  }
	  else{
	    //fprintf(stdout, "Alignment found for %lu with %lu threads at %lu with magic %lu\n", i,j ,bufsize,magic);
	  }
	}

      }
    }
    fprintf(stdout,"\n");
  }
  return 0;
}
