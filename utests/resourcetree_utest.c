#include <stdlib.h>
#include "../src/streamer.h"
#include "../src/resourcetree.h"
#include "../src/common_filehandling.h"
#include "resourcetree.h"
#include "common.h"
#define ENTITIES 200
#define ENT_TYPE_INT 0
#define ENT_TYPE_LONG 1


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
  return retval;
}
int main(void)
{

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


  return 0;
}
