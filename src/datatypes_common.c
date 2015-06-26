#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "datatypes_common.h"
#include "logging.h"

int get_sec_from_mark5b(void *buffer)
{
  int day,sec;
  int n;
  if((n=sscanf((char*)POINT_TO_MARK5B_SECOND(buffer), "%03d%05d", &day, &sec)) != 2){
    E("sscanf got only %d from buffer", n);
    return -1;
  }
  return sec;
}
int get_day_from_mark5b(void *buffer)
{
  int day,sec;
  int n;
  if((n=sscanf((char*)POINT_TO_MARK5B_SECOND(buffer), "%03d%05d", &day, &sec)) != 2){
    E("sscanf got only %d from buffer", n);
    return -1;
  }
  return day;
}
int get_sec_and_day_from_mark5b(void *buffer, int * sec, int * day)
{
  char temp[10];
  memset(&temp, 0, sizeof(char)*10);
  sprintf(temp, "%X", DAY_FROM_MARK5B(buffer));
  *day = atoi(temp);
  memset(&temp, 0, sizeof(char)*10);
  //sprintf(temp, "%X", *((uint32_t*)(POINT_TO_MARK5B_SECOND(buffer))) & RBITMASK_20);
  sprintf(temp, "%X", SECOND_FROM_MARK5B(buffer));
  *sec = atoi(temp);
  //D("day %d sec %d", *day, *sec);
  //D("At point: %X", *((uint32_t*)(POINT_TO_MARK5B_SECOND(buffer))));
  return 0;
  /*
  int err = sscanf(((char*)(POINT_TO_MARK5B_SECOND(buffer))), "%03d%05d", day, sec);
  if(err != 2)
  return err;
  */
}
int get_sec_and_day_from_mark5b_net(void *buffer, int * sec, int * day)
{
  return get_sec_and_day_from_mark5b(buffer+8, sec, day);
}
long epochtime_from_mark5b(void *buffer, struct tm* reftime)
{
  /* Presumes that reftime has the same stuff mark5b metadata has */
  int m5seconds, m5days;
  int err;
  err = get_sec_and_day_from_mark5b(buffer, &m5seconds, &m5days);
  if(err != 0)
  {
    E("Didnt't get day and sec from sscanf. Got %d", err);
    return ERROR_IN_DIFF;
  }
  /* We will presume its about on the same day */

  /* Oh **** its Modified julian date crap */
  /*
  if(m5days != reftime->tm_yday){
    D("Different day in mark5data: in mark5b: %d, right now %d. Better just wait than to try to count this..", m5days, reftime->tm_yday);
    return DIFFERENT_DAY;
  }
  */
  /* Dropping day checking altogether. Just going to check if time within day matches */
  
  /* What a silly way. How bout just get second of day atm. and figure this out faster*/
  struct tm m5tm;
  memset(&m5tm, 0, sizeof(struct tm));
  m5tm.tm_hour = m5seconds / (60*60);
  m5tm.tm_min = (m5seconds % (60*60))/60;
  m5tm.tm_sec = ((m5seconds % (60*60)) % 60);
  m5tm.tm_year = reftime->tm_year;
  m5tm.tm_mon = reftime->tm_mon;
  m5tm.tm_mday = reftime->tm_mday;


  /* Manpage claimed mktime handles these nicely */
  if(reftime->tm_hour ==  23 && m5seconds < MIDNIGHTRESHOLD){
    /* m5seconds is tomorrow */
    m5tm.tm_mday++;
  }
  else if (reftime->tm_hour < 0 && m5seconds > (SECONDS_IN_DAY-MIDNIGHTRESHOLD)){
    /* m5seconds is yesterday */
    m5tm.tm_mday--;
  }

  return (int64_t)mktime(&m5tm) - timezone;
}
int syncword_check(void *buffer)
{
  if(*((uint32_t*)(buffer)) == MARK5BSYNCWORD)
    return 1;
  return 0;
}
int secdiff_from_mark5b_net(void *buffer, struct tm* reftime, int *errref)
{
  if(syncword_check(buffer+8)!= 1)
  {
    //D("Got %X when expected %X", *((int32_t*)(buffer+8)), MARK5BSYNCWORD);
    if(errref != NULL)
      *errref = NONEVEN_PACKET;
    return NONEVEN_PACKET;
  }
  return secdiff_from_mark5b(buffer+8, reftime,errref);
}
int secdiff_from_mark5b(void *buffer, struct tm* reftime, int *errref)
{
  /* Presumes that reftime has the same stuff mark5b metadata has */
  int m5seconds, m5days;
  int err;
  int diff;
  err = get_sec_and_day_from_mark5b(buffer, &m5seconds, &m5days);
  /* Yeah pass an error int pointer but.. int32_min is a difference of 68 years.*/
  /* TODO: Ask me to debug this in 2038 */
  if(err != 0)
  {
    E("Didnt't get day and sec from sscanf. Got %d", err);
    if(errref != NULL)
      *errref = -1;
    return ERROR_IN_DIFF;
  }
  /* We will presume its about on the same day */

  int secofday = reftime->tm_hour*60*60 + reftime->tm_min*60 + reftime->tm_sec;

  /* Format here: Positive = m5 is later, negative = m5 is ahead	*/
  /* Check if we're close to midnight */
  if(secofday > SECONDS_IN_DAY-MIDNIGHTRESHOLD && m5seconds < MIDNIGHTRESHOLD){
    diff = -((SECONDS_IN_DAY-secofday) + m5seconds);
  }
  else if (secofday < MIDNIGHTRESHOLD && m5seconds > (SECONDS_IN_DAY-MIDNIGHTRESHOLD)){
    diff = ((SECONDS_IN_DAY-m5seconds) + secofday);
  }
  else
    diff = secofday - m5seconds;
    //diff = m5seconds-secofday;

  //D("Diff is %d. Buffer has %d and secofday is %d", diff, m5seconds,secofday);
  return diff;
}
long epochtime_from_vdif(void *buffer, struct tm* reftime)
{
  (void)buffer;
  (void)reftime;

  return 0;
}
uint64_t get_datatype_from_string(char * match)
{
  if (!strcmp(match,"unknown"))
    return DATATYPE_UNKNOWN;
  if (!(strcmp(match,"vdif")))
    return DATATYPE_VDIF;
  if (!(strcmp(match,"mark5bnet")))
    return DATATYPE_MARK5BNET;
  if (!(strcmp(match,"mark5b")))
    return DATATYPE_MARK5B;
  if (strcmp(match,"udpmon") == 0)
    return DATATYPE_UDPMON;
  LOG("No datatype known as %s %d\n", match,strcmp(match,"udpmon"));
  return 0;
}
long epochtime_from_mark5b_net(void *buffer, struct tm* reftime)
{
  if(syncword_check(buffer+8) != 1)
    return NONEVEN_PACKET;
  else
    return epochtime_from_mark5b(buffer+8, reftime);
}
uint64_t get_a_count(void * buffer, int wordsize, int offset, int change_endianess)
{
  if(wordsize == 4){
    uint32_t temp;
    temp = *((uint32_t*)buffer+offset);
    if(change_endianess == 1)
      return (uint64_t)(be32toh(temp));
    return (uint64_t)temp;
  }
  else if (wordsize == 8){
    uint64_t temp;
    temp = *((uint64_t*)buffer+offset);
    if(change_endianess ==1)
      return be64toh(temp);
    return temp;
  }
  E("wordsize %d not supported", wordsize);
  return -1;
}
long getseq_mark5b_net(void* header){
  return (long)(*((int*)(header+4)));
}
uint64_t getseq_udpmon(void* header){
  //return be64toh(*((long*)header));
  return get_a_count(header, HSIZE_UDPMON, 0, 1);
}
int increment_header(void * modelheader, uint64_t datatype)
{
  switch(datatype & LOCKER_DATATYPE)
  {
    case DATATYPE_VDIF:
      //memcpy(buffer,modelheader,HSIZE_VDIF
      break;
    case DATATYPE_MARK5B:
      //memcpy(buffer,modelheader,HSIZE_MARK5B
      break;
    case DATATYPE_UDPMON:
      SET_FRAMENUM_FOR_UDPMON(modelheader,getseq_udpmon(modelheader)+1);
      break;
    case DATATYPE_MARK5BNET:
      SET_FRAMENUM_FOR_MARK5BNET(modelheader, getseq_mark5b_net(modelheader)+1);
      break;
    default:
      E("Unknown datatype");
      return -1;
  }
  return 0;
}
long getseq_vdif(void* header, struct resq_info *resq){
  long returnable;
  long second = SECOND_FROM_VDIF(header);
  long framenum = FRAMENUM_FROM_VDIF(header);
  //D("Dat second %lu, dat framenum %lu", second, framenum);
  if(resq->starting_second == -1){
    //D("Got first second as %lu, framenum %lu", second, framenum);
    resq->starting_second =  second;
    return framenum;
  }
  if(resq->packets_per_second == -1){
    if(second == resq->current_seq)
      return framenum;
    else{
      //D("got packets per seconds as %lu", resq->current_seq);
      resq->packets_per_second = resq->current_seq;
      resq->packetsecdif = second - resq->starting_second;
    }
  }
  
  if(resq->packets_per_second == 0){
    returnable = (second - resq->starting_second)/resq->packetsecdif;
  }
  else
    returnable =  (second - (long)resq->starting_second)*((long)resq->packets_per_second) + framenum;
  //D("Returning %lu", returnable);
  return returnable;
}
int copy_metadata(void* target, void* source, uint64_t type)
{
  switch(type & LOCKER_DATATYPE)
  {
    case DATATYPE_VDIF:
      memcpy(target, source, HSIZE_VDIF);
      break;
    case DATATYPE_MARK5B:
      memcpy(target,source, HSIZE_MARK5B);
      break;
    case DATATYPE_UDPMON:
      memcpy(target,source, HSIZE_UDPMON);
      break;
    case DATATYPE_MARK5BNET:
      memcpy(target,source, HSIZE_MARK5BNET);
      break;
    default:
      E("Unknown datatype");
      return -1;
  }
  return 0;
}
