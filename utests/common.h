#ifndef COMMON_UTEST_H
#define COMMON_UTEST_H
#include "streamer.h"
#define TEST_START(name) fprintf(stdout, "TEST_START: " #name "\n")
#define TEST_END(name) fprintf(stdout, "TEST_END: " #name "\n")
#define THREAD_EXIT_ON_ERROR(x) do{if(x){E("Fail!"); td->status = THREAD_STATUS_ERROR;pthread_exit(NULL);}}while(0)
#define THREAD_EXIT_ERROR(x) do{E("Error on thread %i", td->thread_id);E(x); td->status = THREAD_STATUS_ERROR;pthread_exit(NULL);}while(0)

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

/* http://www.cut-the-knot.org/recurrence/conversion.shtml */

#endif
