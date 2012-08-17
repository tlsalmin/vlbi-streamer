#include <unistd.h>
#include <string.h>
#include "timer.h"
#define SLEEPCHECK_LOOPTIMES 100
/*
void specadd(struct timespec * to, struct timespec *from){
  if(to->tv_nsec + from->tv_nsec >  BILLION){
    to->tv_sec++;
    to->tv_nsec += (BILLION-to->tv_nsec)+from->tv_nsec;
  }
  else{
    to->tv_nsec+=from->tv_nsec;
    to->tv_sec+=from->tv_sec;
  }
}
*/
/* Return the diff of the two timespecs in nanoseconds */
long nanodiff(TIMERTYPE * start, TIMERTYPE *end){
  unsigned long temp=0;
  temp += (end->tv_sec-start->tv_sec)*BILLION;
#ifdef TIMERTYPE_GETTIMEOFDAY
  temp += (end->tv_usec-start->tv_usec)*1000;
#else
  temp += end->tv_nsec-start->tv_nsec;
#endif
  return temp;
}
void nanoadd(TIMERTYPE * datime, unsigned long nanos_to_add){
#ifdef TIMERTYPE_GETTIMEOFDAY
  if(datime->tv_usec*1000 + nanos_to_add > BILLION)
#else
    if(datime->tv_nsec + nanos_to_add >  BILLION)
#endif
    {
      datime->tv_sec++;
#ifdef TIMERTYPE_GETTIMEOFDAY
      datime->tv_usec += (MILLION-datime->tv_usec)+nanos_to_add/1000;
#else
      datime->tv_nsec += (BILLION-datime->tv_nsec)+nanos_to_add;
#endif
    }
    else
    {
#ifdef TIMERTYPE_GETTIMEOFDAY
      datime->tv_usec += nanos_to_add/1000;
#else
      datime->tv_nsec += nanos_to_add;
#endif
    }
}
int get_sec_diff(TIMERTYPE *timenow, TIMERTYPE* event){
  int diff = 0;
  /* Straight second diff */
  diff += event->tv_sec - timenow->tv_sec;
#ifdef TIMERTYPE_GETTIMEOFDAY
  diff += (event->tv_usec-timenow->tv_usec)/MILLION;
#else
  diff += (event->tv_nsec-timenow->tv_nsec)/BILLION;
#endif
  return diff;
}
void zeroandadd(TIMERTYPE *datime, unsigned long nanos_to_add){
  /*
     datime->tv_sec = 0;
     datime->tv_nsec = 0;
     */
  ZEROTIME((*datime));
  nanoadd(datime,nanos_to_add);
}
unsigned long get_min_sleeptime(){
  unsigned long cumul = 0;
  int i;
  TIMERTYPE start,end;
  ZEROTIME(start);
  for(i=0;i<SLEEPCHECK_LOOPTIMES;i++){ 
    ZEROTIME(end);
    nanoadd(&end,1);
    GETTIME(start);
    SLEEP_NANOS(end);
    GETTIME(end);
    cumul+= nanodiff(&start,&end);
  }
  return cumul/SLEEPCHECK_LOOPTIMES;
}
