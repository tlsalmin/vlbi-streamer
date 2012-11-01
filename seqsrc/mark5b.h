#include "common.h"
//#define MARK5BISDERP
#define USERSPECCHARLENGTH 4
int m5getMJD(int theword){
  int returnable = 0;
#ifdef MARK5BISDERP
  char ohboi;
  ohboi = 0;
  ohboi |= GRAB_AND_SHIFT(theword,28,31);
  returnable += atoi(&ohboi)*100;
  ohboi = 0;
  ohboi |= GRAB_AND_SHIFT(theword,24,27);
  returnable += atoi(&ohboi)*10;
  ohboi = 0;
  ohboi |= GRAB_AND_SHIFT(theword,20,23);
  returnable += atoi(&ohboi);
#else
  returnable += ((theword & get_mask(28,31)) >> 28)*100;
  returnable += ((theword & get_mask(24,27)) >> 24)*10;
  returnable += ((theword & get_mask(20,23)) >> 20);
#endif
  return returnable;
}
int m5getsecs(int theword){
  int returnable = 0;
#ifdef MARK5BISDERP
  char ohboi;
  ohboi = 0;
  ohboi |= GRAB_AND_SHIFT(theword,16,19);
  returnable += atoi(&ohboi)*10000;
  ohboi = 0;
  ohboi |= GRAB_AND_SHIFT(theword,12,15);
  returnable += atoi(&ohboi)*1000;
  ohboi = 0;
  ohboi |= GRAB_AND_SHIFT(theword,8,11);
  returnable += atoi(&ohboi)*100;
  ohboi = 0;
  ohboi |= GRAB_AND_SHIFT(theword,4,7);
  returnable += atoi(&ohboi)*10;
  ohboi = 0;
  ohboi |= GRAB_AND_SHIFT(theword,0,3);
  returnable += atoi(&ohboi);
#else
  returnable += ((theword & get_mask(16,19)) >> 16)*10000;
  returnable += ((theword & get_mask(12,15)) >> 12)*1000;
  returnable += ((theword & get_mask(8,11)) >> 8)*100;
  returnable += ((theword & get_mask(4,7) >> 4))*10;
  returnable += ((theword & get_mask(0,3)));
#endif
  return returnable;
}
long m5getmyysecs(unsigned int theword){
  unsigned int returnable = 0;
  returnable += ((theword & get_mask(28,31)) >> 28)*100000;
  returnable += ((theword & get_mask(24,27)) >> 24)*10000;
  returnable += ((theword & get_mask(20,23)) >> 20)*1000;
  returnable += ((theword & get_mask(16,19) >> 16))*100;
  return (int)returnable;
}
