#include <stdlib.h>
#include "../src/active_file_index.h"
#include "common.h"
#include "../src/streamer.h"

#define THREADS 100
#define AFILES 10
#define FILES_PER_AFILE 1000

#define THREAD_STATUS_NOT_STARTED 	1
#define THREAD_STATUS_STARTED 		2
#define THREAD_STATUS_ERROR 		4
#define THREAD_STATUS_FINISHED 		8

char ** filenames;
struct thread_data {
  int thread_id;
  int status;
  char* filename;
  pthread_t ptd;
};

void *testfunc(void *tdr)
{
  struct thread_data* td = (struct thread_data*)tdr;

  td->status = THREAD_STATUS_FINISHED;
  E("Hello");
  pthread_exit(NULL);
}

int main(void)
{
  int i, err, retval=0, errors=0;
  struct thread_data thread_data[THREADS];

  filenames = (char**)malloc(sizeof(char*)*AFILES);
  for(i=0;i<AFILES;i++){
    filenames[i] = (char*) malloc(sizeof(char)*12);
    sprintf(filenames[i], "%s%d", "filename", i);
  }

  for(i=0;i<THREADS;i++)
  {
    thread_data[i].thread_id = i; 
    thread_data[i].status = THREAD_STATUS_NOT_STARTED; 
    thread_data[i].filename = filenames[i % 10];
  }

  TEST_START(INIT);
  if(init_active_file_index() != 0)
    return -1;
  TEST_END(INIT);

  for(i=0;i<THREADS;i++)
  {
    err = pthread_create(&thread_data[i].ptd, NULL, testfunc, (void*)&thread_data[i]);
    if(err != 0)
      E("Error in thread init for %ld",, i);
  }

  for(i=0;i<THREADS;i++)
  {
    err = pthread_join(thread_data[i].ptd, NULL);
    if(err != 0)
      E("Error in pthread join for %ld",, i);
  }

  for(i=0;i<THREADS;i++)
  {
    if(thread_data[i].status & (THREAD_STATUS_ERROR | THREAD_STATUS_NOT_STARTED))
    {
      errors++;
      retval=-1;
    }
  }
  LOG("Found %ld errors\n", errors);

  for(i=0;i<AFILES;i++){
    free(filenames[i]);
  }
  free(filenames);
  return retval;
}
