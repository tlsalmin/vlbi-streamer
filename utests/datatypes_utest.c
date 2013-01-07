#include <stdlib.h>
#include "../src/datatypes.h"
#include "common.h"
#include "../src/streamer.h"
#include "../src/config.h"
#include "string.h"

#define N_PACKETS 6500
#define PACKET_SIZE 1200
#define RANDOM_FILE 4
#define DO_ERRORS 1000

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
  int * expected_errors = malloc(sizeof(int));
  void* packet;
  D("Initalizing buffer");
  for(i=0;i<opt->buf_num_elems;i++)
  {
    packet = testarea + i*(opt->packet_size);
    switch(opt->optbits & LOCKER_DATATYPE)
    {
      case DATATYPE_VDIF:
	SET_FRAMENUM_FOR_VDIF(packet,i);
	SET_SECOND_FOR_VDIF(packet,i);
	break;
      case DATATYPE_UDPMON:
	SET_FRAMENUM_FOR_UDPMON(packet,i);
	break;
      case DATATYPE_MARK5BNET:
	SET_FRAMENUM_FOR_MARK5BNET(packet,i);
	break;
      case DATATYPE_MARK5B:
	SET_FRAMENUM_FOR_MARK5B(packet,i);
	//TODO
	break;
    }
    if(i==0)
      copy_metadata(opt->first_packet, testarea,opt);
  }

  *expected_errors = 0;
  err = check_and_fill(testarea,opt,0,expected_errors);
  CHECK_ERR("Check and fill");
  if(*expected_errors != 0){
    E("Found errors when shouldn't!");
    return -1;
  }
  D("Default situation checked ok. Testing file n-situation");
  for(i=0;i<opt->buf_num_elems;i++)
  {
    packet = testarea + i*(opt->packet_size);
    switch(opt->optbits & LOCKER_DATATYPE)
    {
      case DATATYPE_VDIF:
	SET_FRAMENUM_FOR_VDIF(packet,i);
	SET_SECOND_FOR_VDIF(packet,i);
	//TODO properly
	break;
      case DATATYPE_UDPMON:
	SET_FRAMENUM_FOR_UDPMON(packet,i+RANDOM_FILE*opt->buf_num_elems);
	break;
      case DATATYPE_MARK5BNET:
	SET_FRAMENUM_FOR_MARK5BNET(packet,i+RANDOM_FILE*(opt->buf_num_elems));
	break;
      case DATATYPE_MARK5B:
	SET_FRAMENUM_FOR_MARK5B(packet,i+RANDOM_FILE*(opt->buf_num_elems));
	//TODO
	break;
    }
  }
  *expected_errors = 0;
  err = check_and_fill(testarea,opt,RANDOM_FILE, expected_errors);
  CHECK_ERR("Check and fill fileid");
  if(*expected_errors != 0){
    E("Found errors when shouldn't!");
    return -1;
  }
  D("Doing random writes to produce some errors");
  for(i=0;i<DO_ERRORS;i++)
  {
    int packetnum = (rand() % (opt->buf_num_elems-1));
    D("Doing error in packetnum %d",, packetnum);
    memset(testarea+packetnum*(opt->packet_size), 0, 32);
  }
  *expected_errors = DO_ERRORS;
  err = check_and_fill(testarea,opt,RANDOM_FILE, expected_errors);
  CHECK_ERR("Check and fill fileid");
  if(*expected_errors > 0 && *expected_errors <= DO_ERRORS)
  {
    D("Amount of errors reasonable: %d",, *expected_errors);
  }
  else{
    E("Unreasonable amount of errors: %d",, *expected_errors);
    return -1;
  }
  D("Checking fixed area");
  *expected_errors = 0;
  err = check_and_fill(testarea,opt,RANDOM_FILE, expected_errors);
  CHECK_ERR("Check and fill fileid");
  if(*expected_errors != 0){
    E("Found errors when shouldn't!");
    return -1;
  }
  return 0;
}

int main(void)
{
  int retval =0;
  opt = malloc(sizeof(struct opt_s));
  clear_and_default(opt,0);
  opt->buf_num_elems = N_PACKETS;
  opt->packet_size = PACKET_SIZE;
  testarea = malloc((opt->packet_size)*(opt->buf_num_elems));

  opt->first_packet = malloc(opt->packet_size);

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
