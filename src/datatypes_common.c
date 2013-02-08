#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "datatypes_common.h"
#include "logging.h"
int get_sec_from_mark5b(void *buffer)
{
  int day,sec;
  int n;
  if((n=sscanf((char*)POINT_TO_MARK5B_SECOND(buffer), "%03d%05d", &day, &sec)) != 2){
    E("sscanf got only %d from buffer",, n);
    return -1;
  }
  return sec;
}
int get_day_from_mark5b(void *buffer)
{
  int day,sec;
  int n;
  if((n=sscanf((char*)POINT_TO_MARK5B_SECOND(buffer), "%03d%05d", &day, &sec)) != 2){
    E("sscanf got only %d from buffer",, n);
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
  //D("day %d sec %d",, *day, *sec);
  //D("At point: %X",, *((uint32_t*)(POINT_TO_MARK5B_SECOND(buffer))));
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
    E("Didnt't get day and sec from sscanf. Got %d",, err);
    return ERROR_IN_DIFF;
  }
  /* We will presume its about on the same day */

  /* Oh **** its Modified julian date crap */
  /*
  if(m5days != reftime->tm_yday){
    D("Different day in mark5data: in mark5b: %d, right now %d. Better just wait than to try to count this..",, m5days, reftime->tm_yday);
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
int secdiff_from_mark5b_net(void *buffer, struct tm* reftime, int *errref)
{
  if(*((uint32_t*)(buffer+8)) != MARK5BSYNCWORD)
  {
    //D("Got %X when expected %X",, *((int32_t*)(buffer+8)), MARK5BSYNCWORD);
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
    E("Didnt't get day and sec from sscanf. Got %d",, err);
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

  //D("Diff is %d. Buffer has %d and secofday is %d",, diff, m5seconds,secofday);
  return diff;
}
long epochtime_from_vdif(void *buffer, struct tm* reftime)
{
  (void)buffer;
  (void)reftime;

  return 0;
}
long epochtime_from_mark5b_net(void *buffer, struct tm* reftime)
{
  if(*((long*)(buffer+8)) != MARK5BSYNCWORD)
    return NONEVEN_PACKET;
  else
    return epochtime_from_mark5b(buffer+8, reftime);
}
