#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include "test_resourcetree.h"
#include "resourcetree.h"
#include "streamer.h"

#define ENTITIES 200
#define ENT_TYPE_INT 0
#define ENT_TYPE_LONG 1

#define N_THREADS 1000
#define AFILES 10

struct listed_entity *gen_n_entities(int n, int ent_type)
{
  int i;
  struct listed_entity *entities =
    (struct listed_entity *)malloc(sizeof(struct listed_entity) * ENTITIES);
  struct listed_entity *tempe = entities;
  for (i = 0; i < n; i++)
    {
      if (ent_type == ENT_TYPE_INT)
        tempe->entity = (int *)malloc(sizeof(int));
      if (ent_type == ENT_TYPE_LONG)
        tempe->entity = (long *)malloc(sizeof(long));
      tempe++;
    }
  return entities;
}

void free_entities(struct listed_entity *ent, int n)
{
  int i = 0;
  struct listed_entity *entities = ent;
  for (i = 0; i < n; i++)
    {
      free(ent->entity);
      ent++;
    }
  free(entities);
}

int sillylompare(void *i1, void *i2)
{
  if (*((long *)i1) > *((long *)i2))
    return 1;
  return 0;
}


START_TEST(test_resourcetree_no_cmp)
{
  int i;
  struct listed_entity *entities = gen_n_entities(ENTITIES, ENT_TYPE_LONG);
  struct listed_entity *root = NULL;
  D("Adding dummy entities");
  for (i = 0; i < ENTITIES; i++)
    {
      add_to_next(&root, &(entities[i]), NULL);
    }
  D("Entities added");
  i = 0;

  while (root != NULL)
    {
      i++;
      root = root->child;
    }
  if (i != ENTITIES)
    {
      ck_abort_msg("Different n of entities in list! n was %d when wanted %d",
                   i, ENTITIES);
    }
  free_entities(entities, ENTITIES);
}
END_TEST

START_TEST(test_resourcetree_cmp)
{
  int i;
  struct listed_entity *entities = gen_n_entities(ENTITIES, ENT_TYPE_LONG);
  struct listed_entity *root = NULL;
  D("Adding dummy entities");
  for (i = 0; i < ENTITIES; i++)
    {
      *((long *)entities[i].entity) = random();
      add_to_next(&root, &(entities[i]), &sillylompare);
    }
  D("Entities added");
  i = 0;

  while (root != NULL)
    {
      i++;
      if (root->child != NULL)
        {
          if (*((long *)root->entity) < *((long *)root->child->entity))
            {
              ck_abort_msg("Wrong order of elems");
            }
        }
      root = root->child;
    }
  if (i != ENTITIES)
    {
      ck_abort_msg("Different n of entities in list! n was %d when wanted %d",
                   i, ENTITIES);
    }
  free_entities(entities, ENTITIES);
}
END_TEST

START_TEST(test_resourcetree_change_branch)
{
  int i, j;
  struct listed_entity *entities = gen_n_entities(ENTITIES, ENT_TYPE_LONG);
  struct listed_entity *entities2 = gen_n_entities(ENTITIES, ENT_TYPE_LONG);
  struct listed_entity *root = NULL;
  struct listed_entity *root2 = NULL;
  D("Adding dummy entities");
  for (i = 0; i < ENTITIES; i++)
    {
      //*((long*)entities[i].entity) = random();
      add_to_next(&root, &(entities[i]), NULL);
    }
  for (i = 0; i < ENTITIES; i++)
    {
      //*((long*)entities[i].entity) = random();
      add_to_next(&root2, &(entities2[i]), NULL);
    }
  D("Entities added");

  for (j = 0; j < 100; j++)
    {
      mutex_free_change_branch(&root2, &entities[j]);
    }

  D("Changed branch");

  i = 0;
  while (root != NULL)
    {
      i++;
      root = root->child;
    }
  j = 0;
  while (root2 != NULL)
    {
      j++;
      root2 = root2->child;
    }
  if (i != ENTITIES - 100)
    {
      ck_abort_msg("Different n of entities in list! n was %d when wanted %d",
                   i, ENTITIES - 100);
    }
  if (j != ENTITIES + 100)
    {
      ck_abort_msg("Different n of entities in list! n was %d when wanted %d",
                   j, ENTITIES + 100);
    }
  free_entities(entities, ENTITIES);
  free_entities(entities2, ENTITIES);
  D("Simple change finished");
}
END_TEST

struct opt_s *opt;
char **filenames;
uint32_t sleeptime;

void *mainloop(void *tdr)
{
  struct thread_data *td = (struct thread_data *)tdr;
  struct recording_entity *re;
  int *acq = malloc(sizeof(int));
  long intid = (long)td->intid;
  //struct file_index *fi;
  td->status = THREAD_STATUS_STARTED;

  D("%i Getting ent with id  %ld!", td->thread_id, intid);
  re = get_free(opt->diskbranch, opt, &intid, acq, 0);
  if (re == NULL)
    {
      ck_abort_msg("Cant get ent");
    }
  if (*acq != 0)
    {
      ck_abort_msg("Acquire failed");
    }

  D("setting %ld as loaded", intid);
  set_loaded(opt->diskbranch, re->self);

  D("getting %ld from loaded", intid);
  re = get_loaded(opt->diskbranch, re->getid(re), opt);
  if (re == NULL)
    {
      ck_abort_msg("Cant get loaded ent");
    }

  D("Setting %d to free", re->getid(re));
  set_free(opt->diskbranch, re->self);

  usleep(sleeptime);

  td->status = THREAD_STATUS_FINISHED;
  pthread_exit(NULL);
}

START_TEST(test_resourcetree_trashing)
{
  struct thread_data thread_data[N_THREADS];
  struct entity_list_branch *br =
    (struct entity_list_branch *)malloc(sizeof(struct entity_list_branch));
  struct opt_s default_opt;
  int err, i;

  clear_and_default(&default_opt, 0);
  default_opt.n_drives = ENTITIES;
  default_opt.diskbranch = br;
  default_opt.optbits &= ~LOCKER_REC;
  default_opt.optbits |= REC_DUMMY;
  /* Silly but check needs it */
  default_opt.status = STATUS_RUNNING;
  opt = &default_opt;

  memset(br, 0, sizeof(struct entity_list_branch));
  sleeptime = 5;

  D("Init lock");
  err = LOCK_INIT(&br->branchlock);
  ck_assert_msg(err == 0, "lock init");
  err = pthread_cond_init(&(br->busysignal), NULL);
  ck_assert_msg(err == 0, "lock init");

  filenames = (char **)malloc(sizeof(char *) * AFILES);
  for (i = 0; i < AFILES; i++)
    {
      filenames[i] = (char *)malloc(sizeof(char) * FILENAME_MAX);
      memset(filenames[i], 0, sizeof(char) * FILENAME_MAX);
      sprintf(filenames[i], "%s%d", "filename", i % 10);
    }
  default_opt.filename = filenames[0];

  for (i = 0; i < N_THREADS; i++)
    {
      thread_data[i].thread_id = i;
      thread_data[i].status = THREAD_STATUS_NOT_STARTED;
      thread_data[i].filename = filenames[i % 10];
      thread_data[i].intid = i * N_THREADS;
    }

  D("Init recpoints");
  err = init_recp(&default_opt);
  ck_assert_msg(err == 0, "init recp");

  for (i = 0; i < N_THREADS; i++)
    {
      err =
        pthread_create(&thread_data[i].ptd, NULL, mainloop,
                       (void *)&thread_data[i]);
      ck_assert_msg(err == 0, "Error in thread init for %d", i);
    }

  for (i = 0; i < N_THREADS; i++)
    {
      err = pthread_join(thread_data[i].ptd, NULL);
      ck_assert_msg(err == 0, "Error in pthread join for %d", i);
    }

  for (i = 0; i < N_THREADS; i++)
    {
      if (!(thread_data[i].status == THREAD_STATUS_FINISHED))
        {
          ck_abort();
        }
    }

  close_recp(&default_opt, NULL);

  for (i = 0; i < AFILES; i++)
    {
      free(filenames[i]);
    }
  free(filenames);
}
END_TEST

void add_my_tests(TCase * core)
{
  tcase_add_test(core, test_resourcetree_no_cmp);
  tcase_add_test(core, test_resourcetree_cmp);
  tcase_add_test(core, test_resourcetree_change_branch);
  tcase_add_test(core, test_resourcetree_trashing);
}
