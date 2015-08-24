#include <pthread.h>
#include <stdio.h>
#include "test_afi.h"
#include "active_file_index.c"

START_TEST(test_afi_init)
{
  int err = afi_init();
  ck_assert(err == 0);
  err = afi_close();
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

  fi = afi_add(td->filename, 0, AFI_SEND, 0);
  ck_assert(fi != NULL);

  for (i = 0; i < FILES_PER_AFILE; i++)
    {
      err = afi_add_file(fi, td->intid, td->intid, FH_ONDISK);
      ck_assert(err == 0);
      td->intid++;
    }

  td->intid -= FILES_PER_AFILE;

  for (i = 0; i < FILES_PER_AFILE; i++)
    {
      err =
        afi_update_fh_wname(td->filename, td->intid++, FH_INMEM,
                                       AFI_ADD_TO_STATUS);
      ck_assert(err == 0);
    }

  td->intid -= FILES_PER_AFILE;
  for (i = 0; i < FILES_PER_AFILE; i++)
    {
      err = afi_mark_recid_missing(td->filename, td->intid++);
      ck_assert(err == 0);
    }

  LOG("Disassociating\n");
  err = afi_disassociate(fi, AFI_SEND);
  ck_assert(err == 0);

  td->status = THREAD_STATUS_FINISHED;
  pthread_exit(NULL);
}

START_TEST(test_afi_trashing)
{
  int err, i;
  struct thread_data thread_data[THREADS];

  LOG("Starting trashing test\n");
  err = afi_init();
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

START_TEST(test_fi_add)
{
  struct file_index *fi, *fi_temp;
  int err;
  const char *name = "the_file";
  const size_t n_to_add = 10;
  size_t i;

  err = afi_init();
  ck_assert(err == 0);
  ck_assert(files == NULL);

  // Not finding anything
  fi = afi_get(name, false);
  ck_assert(fi == NULL);

  // Add to empty
  fi = afi_add(name, 0, AFI_RECORD, 5000);
  ck_assert(fi != NULL);
  ck_assert_msg(fi->packet_size == 5000, "Was %zu", fi->packet_size);
  ck_assert(fi->associations == 1);
  ck_assert(fi->allocated_files == INITIAL_SIZE);
  ck_assert(fi->next == NULL);

  // Try to find it.
  fi = afi_get(name, 0);
  ck_assert(fi != NULL);
  ck_assert(fi->associations == 1);
  ck_assert(fi->next == NULL);

  // Try to add another with same name and record.
  fi_temp = afi_add(name, 0, AFI_RECORD, 5000);
  ck_assert(fi_temp == NULL);
  ck_assert(fi->associations == 1);

  // And add one with sending.
  fi_temp = afi_add(name, 0, AFI_SEND, 0);
  ck_assert(fi_temp != NULL);
  ck_assert(!!(fi_temp->status & AFI_RECORD));
  ck_assert(!!(fi_temp->status & AFI_SEND));
  ck_assert_msg(fi_temp->associations == 2, "Was %d", fi_temp->associations);

  // disassociate both.
  err = afi_disassociate(fi, AFI_RECORD);
  ck_assert(err == 0);
  err = afi_disassociate(fi_temp, AFI_SEND);
  ck_assert(err == 0);

  ck_assert_msg(files == NULL, "Was %p", files);

  // Add a bunch.
  for (i = 0; i < n_to_add; i++)
    {
      char namebuf[BUFSIZ];

      snprintf(namebuf, BUFSIZ, "file %zu", i);
      fi = afi_add(namebuf, 0, AFI_RECORD, 5000);
      ck_assert(fi != NULL);

      fi_temp = afi_get(namebuf, true);
      ck_assert(fi_temp != NULL);
      ck_assert(fi_temp->associations == 2);
    }
  for (i = 0; i < n_to_add; i++)
    {
      char namebuf[BUFSIZ];

      snprintf(namebuf, BUFSIZ, "file %zu", i);

      fi = afi_get(namebuf, false);
      ck_assert(fi != NULL);
      err = afi_disassociate(fi, AFI_RECORD);
      ck_assert(err == 0);
      err = afi_disassociate(fi, AFI_SEND);
      ck_assert(err == 0);
    }
  ck_assert(files == NULL);
}
END_TEST

void add_my_tests(TCase * core)
{
  tcase_add_test(core, test_afi_init);
  tcase_add_test(core, test_afi_trashing);
  tcase_add_test(core, test_fi_add);
}
