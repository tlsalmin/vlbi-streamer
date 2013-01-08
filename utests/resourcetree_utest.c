#include <stdlib.h>
#include <stdio.h>
#include "../src/streamer.h"
#include "../src/resourcetree.h"
#include "../src/common_filehandling.h"
#include "../src/dummywriter.h"
#include "resourcetree.h"
#include "common.h"
#include <string.h>
#define ENTITIES 200
#define ENT_TYPE_INT 0
#define ENT_TYPE_LONG 1

#define N_THREADS 500
#define AFILES 10

char ** filenames;
struct opt_s * opt;

int sillycompare(void* i1, void *i2){
  if(*((int*)i1) > *((int*)i2))
    return 1;
  return 0;
}
int sillylompare(void* i1, void *i2){
  if(*((long*)i1) > *((long*)i2))
    return 1;
  return 0;
}
struct listed_entity* gen_n_entities(int n, int ent_type){
  int i;
  struct listed_entity * entities = (struct listed_entity*)malloc(sizeof(struct listed_entity)*ENTITIES);
  struct listed_entity * tempe = entities;
  for(i=0;i<n;i++){
    if(ent_type == ENT_TYPE_INT)
      tempe->entity = (int*)malloc(sizeof(int));
    if(ent_type == ENT_TYPE_LONG)
      tempe->entity = (long*)malloc(sizeof(long));
    tempe++;
  }
  return entities;
}
void free_entities(struct listed_entity* ent, int n){
  int i =0;
  struct listed_entity *entities = ent;
  for(i=0;i<n;i++){
    free(ent->entity);
    ent++;
  }
  free(entities);
}
int simple_test_no_compare(){
  int i;
  int retval = 0;
  struct listed_entity *entities = gen_n_entities(ENTITIES, ENT_TYPE_LONG);
  struct listed_entity *root = NULL;
  D("Adding dummy entities");
  for(i=0;i<ENTITIES;i++){
    add_to_next(&root, &(entities[i]), NULL);
  }
  D("Entities added");
  i=0;

  while(root != NULL){
    i++;
    root = root->child;
  }
  if(i != ENTITIES){
    E("Different n of entities in list! n was %d when wanted %d",, i, ENTITIES);
    retval = -1;
  }
  free_entities(entities, ENTITIES);
  return retval;
}
int simple_test_compare(){
  int i;
  int retval = 0;
  struct listed_entity *entities = gen_n_entities(ENTITIES, ENT_TYPE_LONG);
  struct listed_entity *root = NULL;
  D("Adding dummy entities");
  for(i=0;i<ENTITIES;i++){
    *((long*)entities[i].entity) = random();
    add_to_next(&root, &(entities[i]), &sillylompare);
  }
  D("Entities added");
  i=0;

  while(root != NULL){
    i++;
    if(root->child != NULL){
      if(*((long*)root->entity) < *((long*)root->child->entity)){
	D("Wrong order of elems");
	retval = -1;
      }
    }
    root = root->child;
  }
  if(i != ENTITIES){
    E("Different n of entities in list! n was %d when wanted %d",, i, ENTITIES);
    retval = -1;
  }
  free_entities(entities, ENTITIES);
  return retval;
}
int simple_test_change_branches(){
  int i, j;
  int retval = 0;
  struct listed_entity *entities = gen_n_entities(ENTITIES, ENT_TYPE_LONG);
  struct listed_entity *entities2 = gen_n_entities(ENTITIES, ENT_TYPE_LONG);
  struct listed_entity *root = NULL;
  struct listed_entity *root2 = NULL;
  D("Adding dummy entities");
  for(i=0;i<ENTITIES;i++){
    //*((long*)entities[i].entity) = random();
    add_to_next(&root, &(entities[i]), NULL);
  }
  for(i=0;i<ENTITIES;i++){
    //*((long*)entities[i].entity) = random();
    add_to_next(&root2, &(entities2[i]), NULL);
  }
  D("Entities added");

  for(j=0;j<100;j++){
    mutex_free_change_branch(&root, &root2, &entities[j]);
  }

  D("Changed branch");

  i=0;
  while(root != NULL){
    i++;
    root = root->child;
  }
  j=0;
  while(root2 != NULL){
    j++;
    root2 = root2->child;
  }
  if(i != ENTITIES-100){
    E("Different n of entities in list! n was %d when wanted %d",, i, ENTITIES-100);
    retval = -1;
  }
  if(j != ENTITIES+100){
    E("Different n of entities in list! n was %d when wanted %d",, j, ENTITIES+100);
    retval = -1;
  }
  free_entities(entities, ENTITIES);
  free_entities(entities2, ENTITIES);
  D("Simple change finished");
  return retval;
}
void * mainloop(void* tdr)
{
  struct thread_data* td = (struct thread_data*)tdr;
  struct recording_entity * re;
  int * acq = malloc(sizeof(int));
  long intid = (long)td->intid;
  //struct file_index *fi;
  td->status = THREAD_STATUS_STARTED;

  D("%i Getting ent with id  %ld!",, td->thread_id, intid);
  re = get_free(opt->diskbranch, opt, &intid, acq);
  if(re == NULL){
    THREAD_EXIT_ERROR("Cant get ent");
  }
  
  set_free(opt->diskbranch, re->self);

  td->status = THREAD_STATUS_FINISHED;

  free(acq);
  pthread_exit(NULL);
}
int main(void)
{
  int i,err,errors=0,retval=0;

  TEST_START(simple_test_no_compare);
  if(simple_test_no_compare() != 0)
    return -1;
  TEST_END(simple_test_no_compare);

  TEST_START(simple_test_compare);
  if(simple_test_compare() != 0)
    return -1;
  TEST_END(simple_test_compare);

  TEST_START(simple_test_change_branches);
  if(simple_test_change_branches() != 0)
    return -1;
  TEST_END(simple_test_change_branches);


  struct thread_data thread_data[N_THREADS];
  struct entity_list_branch *br = (struct entity_list_branch*)malloc(sizeof(struct entity_list_branch));
  struct opt_s default_opt;
  clear_and_default(&default_opt, 0);
  default_opt.n_drives = ENTITIES;
  default_opt.diskbranch = br;
  default_opt.optbits &= ~LOCKER_REC;
  default_opt.optbits |= REC_DUMMY;
  opt = &default_opt;
  
  memset(br, 0, sizeof(struct entity_list_branch));

  D("Init lock");
  err = LOCK_INIT(&br->branchlock);
  CHECK_ERR("lock init");
  err = pthread_cond_init(&(br->busysignal), NULL);
  CHECK_ERR("lock init");

  filenames = (char**)malloc(sizeof(char*)*AFILES);
  for(i=0;i<AFILES;i++){
    filenames[i] = (char*) malloc(sizeof(char)*FILENAME_MAX);
    memset(filenames[i], 0, sizeof(char)*FILENAME_MAX);
    sprintf(filenames[i], "%s%d", "filename", i%10);
  }
  default_opt.filename = filenames[0];

  for(i=0;i<N_THREADS;i++)
  {
    thread_data[i].thread_id = i; 
    thread_data[i].status = THREAD_STATUS_NOT_STARTED; 
    thread_data[i].filename = filenames[i % 10];
    thread_data[i].intid = i*N_THREADS;
  }

  D("Init recpoints");
  err = init_recp(&default_opt);
  CHECK_ERR("init recp");

  TEST_START(THREADS_START_TRASHING);
  for(i=0;i<N_THREADS;i++)
  {
    err = pthread_create(&thread_data[i].ptd, NULL, mainloop, (void*)&thread_data[i]);
    if(err != 0)
      E("Error in thread init for %d",, i);
  }

  for(i=0;i<N_THREADS;i++)
  {
    err = pthread_join(thread_data[i].ptd, NULL);
    if(err != 0)
      E("Error in pthread join for %d",, i);
  }

  for(i=0;i<N_THREADS;i++)
  {
    if(!(thread_data[i].status == THREAD_STATUS_FINISHED))
    {
      errors++;
      retval=-1;
    }
  }
  LOG("Found %d errors\n", errors);
  TEST_END(THREADS_START_TRASHING);

  close_recp(&default_opt, NULL);
  
  for(i=0;i<AFILES;i++){
    free(filenames[i]);
  }
  free(filenames);

  return retval;
}
