#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include "common.h"

#define B(x) (1 << x)
#define MEG			B(20)
#define O(...) fprintf(stdout, __VA_ARGS__)
#define MIN_PACKETSIZE 128
#define MAX_PACKETSIZE 9000
#define BLOCK_ALIGN 4096

void usage(char * command)
{
  O("USAGE: %s <filesize in MB>\n", command);
  exit(-1);
}
int main(int argc, char** argv)
{
  off_t bufsize;
  off_t tempbuffsize;
  long packet_size = MIN_PACKETSIZE;
  //long buf_num_elems;
  long loss;
  //long buf_num_elems_temp;
  if(argc != 2)
    usage(argv[0]);
  bufsize = ((off_t)(atoi(argv[1])))*MEG;

  O("#Bufsize is %ld bytes\n", bufsize);

  while(packet_size <= MAX_PACKETSIZE)
  {
    tempbuffsize = (bufsize/packet_size)*packet_size;
    while(tempbuffsize % BLOCK_ALIGN != 0 && tempbuffsize > 0)
    {
      tempbuffsize-= packet_size;
    }
    loss = bufsize -tempbuffsize ;
    //buf_num_elems_temp = tempbuffsize/packet_size;
    if(tempbuffsize <=0)
      O("%ld %ld\n", packet_size, bufsize);
    O("%ld %ld\n", packet_size, loss);

    packet_size++;
  }

  exit(0);
}
