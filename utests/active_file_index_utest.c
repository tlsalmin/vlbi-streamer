#include <stdlib.h>
#include <string.h>
#include "../src/active_file_index.h"
#include "../src/config.h"
#include "common.h"

#define THREADS 100
#define AFILES 10
#define FILES_PER_AFILE 1000


char ** filenames;


void *testfunc(void *tdr)
{
  int i;
  struct thread_data* td = (struct thread_data*)tdr;
  struct file_index *fi;

  td->status = THREAD_STATUS_STARTED;

  D("Adding file index");
  fi = add_fileindex(td->filename, 0, FILESTATUS_SENDING);
  THREAD_EXIT_ON_ERROR(fi==NULL);

  for(i=0;i<FILES_PER_AFILE;i++){
    THREAD_EXIT_ON_ERROR(add_file(fi,td->intid, td->intid,FH_ONDISK) != 0);
    td->intid++;
  }

  td->intid-=FILES_PER_AFILE;

  for(i=0;i<FILES_PER_AFILE;i++)
    THREAD_EXIT_ON_ERROR(update_fileholder_status_wname(td->filename, td->intid++, FH_INMEM, ADDTOFILESTATUS) != 0);

  td->intid-=FILES_PER_AFILE;
  for(i=0;i<FILES_PER_AFILE;i++)
    THREAD_EXIT_ON_ERROR(remove_specific_from_fileholders(td->filename, td->intid++) != 0);
  
  D("Disassociating");
  THREAD_EXIT_ON_ERROR(disassociate(fi, FILESTATUS_RECORDING) != 0);
  
  td->status = THREAD_STATUS_FINISHED;
  pthread_exit(NULL);
}

int main(void)
{
  int i, err, retval=0, errors=0;
  struct thread_data thread_data[THREADS];

  filenames = (char**)malloc(sizeof(char*)*AFILES);
  for(i=0;i<AFILES;i++){
    filenames[i] = (char*) malloc(sizeof(char)*FILENAME_MAX);
    memset(filenames[i], 0, sizeof(char)*FILENAME_MAX);
    sprintf(filenames[i], "%s%d", "filename", i%10);
  }

  for(i=0;i<THREADS;i++)
  {
    thread_data[i].thread_id = i; 
    thread_data[i].status = THREAD_STATUS_NOT_STARTED; 
    thread_data[i].filename = filenames[i % 10];
    thread_data[i].intid = i*THREADS;
  }

  TEST_START(INIT);
  if(init_active_file_index() != 0)
    return -1;
  TEST_END(INIT);

  TEST_START(THREADS_START_TRASHING);
  for(i=0;i<THREADS;i++)
  {
    err = pthread_create(&thread_data[i].ptd, NULL, testfunc, (void*)&thread_data[i]);
    if(err != 0)
      E("Error in thread init for %d",, i);
  }

  for(i=0;i<THREADS;i++)
  {
    err = pthread_join(thread_data[i].ptd, NULL);
    if(err != 0)
      E("Error in pthread join for %d",, i);
  }

  for(i=0;i<THREADS;i++)
  {
    if(thread_data[i].status & (THREAD_STATUS_ERROR | THREAD_STATUS_NOT_STARTED))
    {
      errors++;
      retval=-1;
    }
  }
  LOG("Found %d errors\n", errors);
  TEST_END(THREADS_START_TRASHING);

  for(i=0;i<AFILES;i++){
    free(filenames[i]);
  }
  free(filenames);
  return retval;
}
