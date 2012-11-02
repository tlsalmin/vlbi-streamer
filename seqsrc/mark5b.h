#ifndef MARK5B_H
#define MARK5B_H
#include "common.h"
//#define MARK5BISDERP
#define USERSPECCHARLENGTH 4
int m5getMJD(int theword){
  int returnable = 0;
  returnable += ((theword & get_mask(28,31)) >> 28)*100;
  returnable += ((theword & get_mask(24,27)) >> 24)*10;
  returnable += ((theword & get_mask(20,23)) >> 20);
  return returnable;
}
int m5getsecs(unsigned int theword){
  unsigned int returnable = 0;
  returnable += ((theword & get_mask(16,19)) >> 16)*10000;
  returnable += ((theword & get_mask(12,15)) >> 12)*1000;
  returnable += ((theword & get_mask(8,11)) >> 8)*100;
  returnable += ((theword & get_mask(4,7) >> 4))*10;
  returnable += ((theword & get_mask(0,3)));
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
#endif
