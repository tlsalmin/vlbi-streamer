#include <pthread.h>
#include "test_afi.h"
#include "active_file_index.h"

START_TEST(test_afi_init)
{
  int err = init_active_file_index();
  ck_assert(err == 0);
  err = close_active_file_index();
  ck_assert(err == 0);
}
END_TEST

#define THREADS 100
#define AFILES 10
#define FILES_PER_AFILE 1000
char **filenames;

void *testfunc(void *tdr)
{
  int i, err;
  struct thread_data *td = (struct thread_data *)tdr;
  struct file_index *fi;

  D("Starting thread %d", td->intid);
  td->status = THREAD_STATUS_STARTED;

  fi = add_fileindex(td->filename, 0, FILESTATUS_SENDING, 0);
  ck_assert(fi != NULL);

  for (i = 0; i < FILES_PER_AFILE; i++)
    {
      err = add_file(fi, td->intid, td->intid, FH_ONDISK);
      ck_assert(err == 0);
      td->intid++;
    }

  td->intid -= FILES_PER_AFILE;

  for (i = 0; i < FILES_PER_AFILE; i++)
    {
      err =
        update_fileholder_status_wname(td->filename, td->intid++, FH_INMEM,
                                       ADDTOFILESTATUS);
      ck_assert(err == 0);
    }

  td->intid -= FILES_PER_AFILE;
  for (i = 0; i < FILES_PER_AFILE; i++)
    {
      err = remove_specific_from_fileholders(td->filename, td->intid++);
      ck_assert(err == 0);
    }

  LOG("Disassociating\n");
  err = disassociate(fi, FILESTATUS_RECORDING);
  ck_assert(err == 0);

  td->status = THREAD_STATUS_FINISHED;
  pthread_exit(NULL);
}

START_TEST(test_afi_trashing)
{
  int err, i;
  struct thread_data thread_data[THREADS];

  LOG("Starting trashing test\n");
  err = init_active_file_index();
  ck_assert_msg(err == 0, "Failed to initialize");

  filenames = (char **)malloc(sizeof(char *) * AFILES);
  for (i = 0; i < AFILES; i++)
    {
      filenames[i] = (char *)malloc(sizeof(char) * FILENAME_MAX);
      memset(filenames[i], 0, sizeof(char) * FILENAME_MAX);
      sprintf(filenames[i], "%s%d", "filename", i % 10);
    }

  for (i = 0; i < THREADS; i++)
    {
      thread_data[i].thread_id = i;
      thread_data[i].status = THREAD_STATUS_NOT_STARTED;
      thread_data[i].filename = filenames[i % 10];
      thread_data[i].intid = i * THREADS;
    }

  for (i = 0; i < THREADS; i++)
    {
      thread_data[i].thread_id = i;
      err = pthread_create(&thread_data[i].ptd, NULL, testfunc,
                           (void *)&thread_data[i]);
      ck_assert(err == 0);
    }

  for (i = 0; i < THREADS; i++)
    {
      err = pthread_join(thread_data[i].ptd, NULL);
      ck_assert(err == 0);
    }

  for (i = 0; i < THREADS; i++)
    {
      ck_assert(!(thread_data[i].status &
                  (THREAD_STATUS_ERROR | THREAD_STATUS_NOT_STARTED)));
    }

  for (i = 0; i < AFILES; i++)
    {
      free(filenames[i]);
    }
  free(filenames);
}
END_TEST

void add_my_tests(TCase * core)
{
  tcase_add_test(core, test_afi_init);
  tcase_add_test(core, test_afi_trashing);
}
