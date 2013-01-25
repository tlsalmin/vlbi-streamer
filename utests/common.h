#ifndef COMMON_UTEST_H
#define COMMON_UTEST_H
#include <string.h>
#include "streamer.h"
#define TEST_START(name) fprintf(stdout, "TEST_START: " #name "\n")
#define TEST_END(name) fprintf(stdout, "TEST_END: " #name "\n")
#define THREAD_EXIT_ON_ERROR(x) do{if(x){E("Fail!"); td->status = THREAD_STATUS_ERROR;pthread_exit(NULL);}}while(0)
#define THREAD_EXIT_ERROR(x) do{E("Error on thread %i",, td->thread_id);E(x); td->status = THREAD_STATUS_ERROR;pthread_exit(NULL);}while(0)

#define THREAD_STATUS_NOT_STARTED 	B(0)
#define THREAD_STATUS_STARTED 		B(1)
#define THREAD_STATUS_ERROR 		B(2)
#define THREAD_STATUS_FINISHED 		B(3)

struct thread_data {
  int thread_id;
  int status;
  int intid;
  char* filename;
  pthread_t ptd;
};

void recursive_basemod(int m, int n, int n_rec, char* temp)
{
  if(m < n){
    sprintf(temp+n_rec, "%d", m);
    D("Finito! m: %d",, m);
  }
  else
  {
    sprintf(temp+n_rec, "%d", m%n);
    D("Dur m: %d, mod: %d",, m, m%n);
    recursive_basemod(m/n,n,n_rec-1, temp);
  }
}
uint32_t form_hexliteral_from_int(uint32_t m)
{
  char temp[33];
  memset(&temp, 0, sizeof(char)*33);
  recursive_basemod(m, 16, 16, temp);
  D("Dat string %s",, temp);
  return atoi(temp);
}

#endif
