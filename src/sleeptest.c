#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "timer.h"

#define TESTRUNLENGTH 1000
#define SLEEPTIMENANOS 45000

FILE* logfile;

int main(int argc, char ** argv)
{
  int i;
  //long average_difference;
  long sum;
  TIMERTYPE now;
  TIMERTYPE temp;
  TIMERTYPE sleeper;
  ZEROTIME(sleeper);
  SETNANOS(sleeper, SLEEPTIMENANOS);
  //int * values = (int*)malloc(sizeof(int)*TESTRUNLENGTH);
  while(1)
  {
    sum =0;
    for(i=0;i<TESTRUNLENGTH;i++){
      GETTIME(now);
      SLEEP_NANOS(sleeper);
      GETTIME(temp);
      sum+=(nanodiff(&now,&temp)-SLEEPTIMENANOS);
      //values[i] = nanodiff(&now,&temp);
    }
    fprintf(stdout, "Average oversleep is %ld\n", sum/TESTRUNLENGTH);
  }

  return 0;
}
