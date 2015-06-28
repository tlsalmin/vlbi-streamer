#ifndef COMMON_CHECK_H
#define COMMON_CHECK_H

#include <unistd.h>
#include <check.h>
#include <stdlib.h>
#include <pthread.h>
#include "logging_main.h"

typedef enum
{
  THREAD_STATUS_NOT_STARTED = 1,
  THREAD_STATUS_STARTED = 2,
  THREAD_STATUS_ERROR = 4,
  THREAD_STATUS_FINISHED = 8,
} thread_status;

struct thread_data
{
  int thread_id;
  int status;
  int intid;
  char *filename;
  pthread_t ptd;
};

void add_my_tests(TCase *core);

Suite * create_them_suites(void)
{
  Suite *s;
  TCase *tc_core;

  s = suite_create(TEST_NAME_IS);

  /* Core test case */
  tc_core = tcase_create("Core");

  add_my_tests(tc_core);
  suite_add_tcase(s, tc_core);

  return s;
}

int main(void)
{
  int number_failed;
  Suite *s;
  SRunner *sr;

  s = create_them_suites();
  sr = srunner_create(s);
  
  srunner_set_log (sr, TEST_NAME_IS ".log");
  srunner_run_all(sr, CK_NORMAL);
  number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

#endif
