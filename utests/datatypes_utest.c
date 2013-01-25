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

  void * teststring = NULL;
  switch(opt->optbits & LOCKER_DATATYPE)
  {
    case DATATYPE_VDIF:
      teststring = malloc(HSIZE_VDIF+4);
      memset(teststring, 0, HSIZE_VDIF+4);
      D("Not implemented yet");
      return 0;
      //TODO properly
      break;
    case DATATYPE_UDPMON:
      D("Not doing this stuff for udpmon");
      return 0;
      break;
    case DATATYPE_MARK5BNET:
      teststring = malloc(HSIZE_MARK5BNET+4);
      memset(teststring, 0, HSIZE_MARK5BNET+4);
      break;
    case DATATYPE_MARK5B:
      teststring = malloc(HSIZE_MARK5B+4);
      memset(teststring, 0, HSIZE_MARK5B+4);
      //TODO
      break;
  }
  int sec, day;
  long lerr;
  int temperr=0, tempdiff;
  TIMERTYPE temptime;
  GETTIME(temptime);
  struct tm  gmtime_s;
  gmtime_r(&(temptime.tv_sec), &gmtime_s);
  switch(opt->optbits & LOCKER_DATATYPE)
  {
    case DATATYPE_VDIF:
      lerr = epochtime_from_vdif((void*)teststring, &gmtime_s);
      break;
    case DATATYPE_MARK5BNET:
      sprintf((char*)(teststring+8+4+4), "%03d%05d", gmtime_s.tm_yday,SEC_OF_DAY_FROM_TM(&gmtime_s)); 
      //*((int*)(teststring+8+4+4)) = 0xgmtim
      lerr = epochtime_from_mark5b_net((void*)teststring, &gmtime_s);
      if(lerr != NONEVEN_PACKET)
      {
	E("epochtime from mark5bnet should return NONEVEN_PACKET when no ABADDEED present");
	return -1;
      }
      *((long*)(teststring+8)) = 0xABADDEED;
      tempdiff = secdiff_from_mark5b_net((void*)teststring, &gmtime_s, &temperr);
      if(temperr != 0){
	E("ERr in getting secdiff");
	return -1;
      }
      if(tempdiff != 0)
      {
	E("Should get 0 for diff. Got %d",, tempdiff);
	return -1;
      }
      lerr = epochtime_from_mark5b_net((void*)teststring, &gmtime_s);
      break;
    case DATATYPE_MARK5B:
      sprintf((char*)(teststring+4+4), "%03d%05d", gmtime_s.tm_yday,SEC_OF_DAY_FROM_TM(&gmtime_s)); 
      tempdiff = secdiff_from_mark5b((void*)teststring, &gmtime_s, &temperr);
      if(temperr != 0){
	E("ERr in getting secdiff");
	return -1;
      }
      if(tempdiff != 0)
      {
	E("Should get 0 for diff");
	return -1;
      }
      lerr = epochtime_from_mark5b((void*)teststring, &gmtime_s);
      break;
  }
  if(lerr != temptime.tv_sec){
    E("didnt get %ld from epochtime-counter, got %ld",, temptime.tv_sec,lerr);
    return -1;
  }
  int temp;
  if(get_sec_dif_from_buf((void*)teststring, &gmtime_s, opt, &temp) != 0)
  {
    E("didnt get zero from sec dif with time now");
    return -1;
  }
  else if ( temp != 0)
  {
    E("Err in retval");
    return -1;
  }

  for(i=0;i<24*60*60;i++)
  {
    switch(opt->optbits & LOCKER_DATATYPE)
    {
      case DATATYPE_VDIF:
	break;
      case DATATYPE_MARK5BNET:
	sprintf((char*)(teststring+8+4+4), "%03d%05d", i%365,i);
	temp = get_sec_and_day_from_mark5b_net((void*)teststring, &sec, &day);
	break;
      case DATATYPE_MARK5B:
	sprintf((char*)(teststring+4+4), "%03d%05d", i%365,i);
	temp = get_sec_and_day_from_mark5b((void*)teststring, &sec, &day);
	break;
    }
    if(temp != 2)
    {
      E("Didnt get both sec and day!");
      return -1;
    }
    if(sec != i)
    {
      E("Got %d for sec when expected %d",, sec, i);
      return -1;
    }
    if(day != i%365)
    {
      E("Got %d for sec when expected %d",, day, i%365);
      return -1;
    }
  }
  free(expected_errors);
  free(teststring);
  return 0;
}

int main(void)
{
  int retval =0;
  void* testarea_mark5b = malloc(HSIZE_MARK5B);
  void* testarea_vdif = malloc(HSIZE_VDIF);
  void* testarea_udpmon = malloc(HSIZE_UDPMON);
  void* testarea_mark5bnet = malloc(HSIZE_MARK5BNET);
  opt = malloc(sizeof(struct opt_s));
  clear_and_default(opt,0);
  opt->buf_num_elems = N_PACKETS;
  opt->packet_size = PACKET_SIZE;
  testarea = malloc((opt->packet_size)*(opt->buf_num_elems));

  opt->first_packet = malloc(opt->packet_size);

  TEST_START(HEXTRICKS);
  int i;
  for(i=0;i<58237215;i++){
    D("%d as %X",, i, form_hexliteral_from_int(i));
  }

  TEST_END(HEXTRICKS);

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
    E("Error in testrun of mark5bnet");
    retval = -1;
  }
  TEST_END(MARK5BNET);

  free(opt->first_packet);
  free(opt);
  free(testarea_mark5b);
  free(testarea_vdif);
  free(testarea_udpmon);
  free(testarea_mark5bnet);
  return retval;
}
