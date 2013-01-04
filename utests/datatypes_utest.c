#include <stdlib.h>
#include "../src/datatypes.h"
#include "common.h"
#include "../src/streamer.h"
#include "../src/config.h"
#include "string.h"

#define N_PACKETS 6500
#define PACKET_SIZE 1200

unsigned int get_mask(int start, int end){
  unsigned int returnable = 0;
  while(start <= end){
    returnable |= B(start);
    start++;
  }
  return returnable;
}


struct opt_s * opt;
void * testarea;

int testrun()
{
  int i, err;
  D("Initalizing buffer");
  for(i=0;i<N_PACKETS;i++)
  {
    void * packet = testarea + i*PACKET_SIZE;
    switch(opt->optbits & LOCKER_DATATYPE)
    {
      case DATATYPE_VDIF:
	SET_FRAMENUM_FOR_VDIF(packet,i);
	SET_SECOND_FOR_VDIF(packet,i);
	memcpy(opt->first_packet, testarea,HSIZE_VDIF);
	break;
      case DATATYPE_UDPMON:
	SET_FRAMENUM_FOR_UDPMON(packet,i);
	memcpy(opt->first_packet, testarea,HSIZE_UDPMON);
	break;
      case DATATYPE_MARK5BNET:
	SET_FRAMENUM_FOR_MARK5BNET(packet,i);
	memcpy(opt->first_packet, testarea,HSIZE_MARK5BNET);
	break;
      case DATATYPE_MARK5B:
	SET_FRAMENUM_FOR_MARK5B(packet,i);
	memcpy(opt->first_packet, testarea,HSIZE_MARK5B);
	//TODO
	break;
    }
  }

  err = check_and_fill(testarea,opt,0);
  CHECK_ERR("Check and fill");
  return 0;
}

int main(void)
{
  int retval =0;
  opt = malloc(sizeof(struct opt_s));
  testarea = malloc(N_PACKETS*PACKET_SIZE);
  clear_and_default(opt,0);

  opt->buf_num_elems = N_PACKETS;
  opt->first_packet = malloc(PACKET_SIZE);

  TEST_START(UDPMON);
  opt->optbits &= ~LOCKER_DATATYPE;
  opt->optbits |= DATATYPE_UDPMON;
  if (testrun() != 0){
    E("Error in testrun of udpmon");
    retval = -1;
  }
  TEST_END(UDPMON);

  TEST_START(MARK5BNET);
  opt->optbits &= ~LOCKER_DATATYPE;
  opt->optbits |= DATATYPE_MARK5BNET;
  if (testrun() != 0){
    E("Error in testrun of udpmon");
    retval = -1;
  }
  TEST_END(MARK5BNET);


  free(opt->first_packet);
  free(opt);
  return retval;
}
