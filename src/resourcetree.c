#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "resourcetree.h"
#include "common_wrt.h"
#include "streamer.h"

void add_to_next(struct listed_entity **root, struct listed_entity *toadd)
{
  toadd->child = NULL;
  toadd->father = NULL;
  if(*root == NULL){
    (*root) = toadd;
  }
  else{
    while((*root)->child != NULL)
      root = &((*root)->child);
    toadd->father = *root;
    //toadd->child = NULL;
    (*root)->child= toadd;
  }
}
/* Initial add */
void add_to_entlist(struct entity_list_branch* br, struct listed_entity* en)
{
  if(br->mutex_free == 0)
    LOCK(&(br->branchlock));
  add_to_next(&(br->freelist), en);
  if(br->mutex_free == 0)
    UNLOCK(&(br->branchlock));
}
void mutex_free_change_branch(struct listed_entity **from, struct listed_entity **to, struct listed_entity *en)
{
  if(en == *from){
    *from = en->child;
    if(en->child != NULL)
      en->child->father = NULL;
  }
  else
  {
    en->father->child = en->child;
    if(en->child != NULL)
      en->child->father = en->father;
  }
  add_to_next(to, en);
}
/* Set this entity into the free to use list		*/
void set_free(struct entity_list_branch *br, struct listed_entity* en)
{
  LOCK(&(br->branchlock));
  //Only special case if the entity is at the start of the list
  D("Changing entity from busy to free");
  mutex_free_change_branch(&(br->busylist), &(br->freelist), en);
  if(en->release != NULL){
    D("Running release on entity");
    int ret = en->release(en->entity);
    if(ret != 0)
      E("Release returned non zero value.(Not handled in any way)");
  }
  D("Entity free'd. Signaling");
  pthread_cond_broadcast(&(br->busysignal));
  UNLOCK(&(br->branchlock));
}
void set_loaded(struct entity_list_branch *br, struct listed_entity* en){
  D("Setting entity to loaded");
  LOCK(&(br->branchlock));
  mutex_free_change_branch(&(br->busylist), &(br->loadedlist), en);
  pthread_cond_broadcast(&(br->busysignal));
  UNLOCK(&(br->branchlock));
}
void mutex_free_set_busy(struct entity_list_branch *br, struct listed_entity* en)
{
  mutex_free_change_branch(&(br->freelist),&(br->busylist), en);
}
void block_until_free(struct entity_list_branch *br, void* val1){
  struct listed_entity * shouldntfind,* checker;
  LOCK(&(br->branchlock));
  do{
    checker = br->busylist;
    shouldntfind = NULL;
    while(checker != NULL && shouldntfind == NULL){
      if(checker->identify(checker->entity, val1, NULL, CHECK_BY_OPTPOINTER) == 1){
	shouldntfind = checker;
	break;
      }
      //if(strcmp(checker->getrecname(checker->entity),recname)== 0){
      else
	checker = checker->child;
    }
    /* Nobody will wake this up if its left to cond_wait */
    if(shouldntfind != NULL){
      pthread_cond_wait(&(br->busysignal), &(br->branchlock));
    }
  }
  while(shouldntfind != NULL);
  
  /* Release all loaded elements */
  for(checker = br->loadedlist;checker != NULL;){
    if(checker->identify(checker->entity, val1, NULL, CHECK_BY_OPTPOINTER) == 1){
      struct listed_entity *temp = checker;
      checker = checker->child;
      D("Moving loaded to free since thread is exiting");
      mutex_free_change_branch(&br->loadedlist, &br->freelist, temp);
      int ret = temp->release(temp->entity);
      if(ret != 0)
	E("Release returned non zero value.(Not handled in any way)");
    }
    else
      checker = checker->child;
  }
  UNLOCK(&(br->branchlock));
}
void remove_from_branch(struct entity_list_branch *br, struct listed_entity *en, int mutex_free){
  D("Removing entity from branch");
  if(!mutex_free){
    LOCK(&(br->branchlock));
  }
  if(en == br->freelist){
    if(en->child != NULL)
      en->child->father = NULL;
    br->freelist = en->child;
  }
  else if(en == br->busylist){
    if(en->child != NULL)
      en->child->father = NULL;
    br->busylist = en->child;
  }
  else if(en == br->loadedlist){
    if(en->child != NULL)
      en->child->father = NULL;
    br->loadedlist = en->child;
  }
  else{
    /* Weird thing. Segfault when en->father is NULL	*/
    /* Shoulnd't happen but happens on vbs_shutdown	*/
    /* with live sending 				*/
    en->father->child = en->child;
    if(en->child != NULL)
      en->child->father = en->father;
  }
  en->child = NULL;
  en->father = NULL;

  /* This close only frees the entity structure, not the underlying opts etc. 	*/
  en->close(en->entity);
  free(en);

  if(!mutex_free){
    /* Signal so waiting threads can exit if the situation is bad(lost writers	*/
    pthread_cond_broadcast(&(br->busysignal));
    UNLOCK(&(br->branchlock));
  }
  D("Entity removed from branch");
}
struct listed_entity * loop_and_check(struct listed_entity* head, void* val1, void* val2, int iden_type){
  while(head != NULL){
    if(head->identify(head->entity, val1, val2, iden_type) == 1){
      return head;
    }
    else{
      head = head->child;
    }
  }
  return NULL;
}
#define FREE_AND_RETURN_LE \
  if(!mutex_free){\
    UNLOCK(&(br->branchlock));\
  }\
return le;

struct listed_entity* get_from_all(struct entity_list_branch *br, void *val1, void * val2, int iden_type, int mutex_free){
  struct listed_entity *le = NULL;
  if(mutex_free == 0){
    LOCK(&(br->branchlock));
  }
  if((le = loop_and_check(br->freelist, val1, val2, iden_type)) != NULL){
    FREE_AND_RETURN_LE
  }
  if((le = loop_and_check(br->busylist, val1, val2, iden_type)) != NULL){
    FREE_AND_RETURN_LE
  }
  if((le = loop_and_check(br->loadedlist, val1, val2, iden_type)) != NULL){
    FREE_AND_RETURN_LE
  }
  if(mutex_free == 0){
    UNLOCK(&(br->branchlock));
  }
  return NULL;
}
struct listed_entity* get_w_check(struct listed_entity **lep, unsigned long seq, struct entity_list_branch* br, void* optmatch){
  //struct listed_entity *temp;
  struct listed_entity *le = NULL;
  struct listed_entity **other = NULL;
  struct listed_entity **other2 = NULL;
  if(*lep == br->freelist){
    other = &br->busylist;
    other2 = &br->loadedlist;
  }
  else if (*lep == br->busylist){
    other = &br->freelist;
    other2 = &br->loadedlist;
  }
  else if (*lep == br->loadedlist){
    other = &br->freelist;
    other2 = &br->busylist;
  }
  else{
    E("Queried list is something weird. Should be loaded,free,or busy of the branch");
    return NULL;
  }
  //le = NULL;
  while(le== NULL){
    le = loop_and_check(*lep, &seq, optmatch, CHECK_BY_SEQ);
    /* If le wasn't found in the list */
    if(le == NULL){
      /* Check if branch is dead */
      D("Checking for dead branch");
      if(*lep == NULL && *other == NULL && *other2 == NULL){
	E("No entities in list. Returning NULL");
	return NULL;
      }
      /* Need to check if it exists at all */
      if(optmatch == NULL){
	D("Looping to check if exists");
	if(loop_and_check(*other, &seq, NULL, CHECK_BY_SEQ) == NULL && loop_and_check(*other2,&seq, NULL, CHECK_BY_SEQ) == NULL){
	  D("Rec point disappeared!");
	  return NULL;
	}
      }
      D("Failed to get specific. Sleeping waiting for %lu",,seq);
      pthread_cond_wait(&(br->busysignal), &(br->branchlock));
      D("Woke up! Checking for %lu again",, seq);
    }
  }
  D("Found specific elem id %lu!",, seq);
  return le;
}
/* Get a loaded buffer with the specific seq */
inline void* get_loaded(struct entity_list_branch *br, unsigned long seq, void* opt){
  D("Querying for loaded entity %lu",, seq);
  LOCK(&(br->branchlock));
  struct listed_entity * temp = get_w_check(&br->loadedlist, seq, br, opt);

  if (temp == NULL){
    D("Nothing to return!");
    UNLOCK(&(br->branchlock));
    return NULL;
  }

  mutex_free_change_branch(&(br->loadedlist), &(br->busylist), temp);
  UNLOCK(&(br->branchlock));
  D("Returning loaded entity");
  return temp->entity;
}
/* Get a specific free entity from branch 		*/
void* get_specific(struct entity_list_branch *br,void * opt,unsigned long seq, unsigned long bufnum, unsigned long id, int* acquire_result)
{
  (void)bufnum;
  LOCK(&(br->branchlock));
  struct listed_entity* temp = get_w_check(&br->freelist, id, br, NULL);

  if(temp ==NULL){
    UNLOCK(&(br->branchlock));
    if(acquire_result !=NULL)
      *acquire_result = -1;
    return NULL;
  }

  mutex_free_change_branch(&(br->freelist), &(br->busylist), temp);
  UNLOCK(&(br->branchlock));
  if(temp->acquire !=NULL){
    D("Running acquire on entity");
    int ret = temp->acquire(temp->entity, opt,((void*)&seq));
    if(acquire_result != NULL)
      *acquire_result = ret;
    else{
      if(ret != 0)
	E("Acquire return non-zero value(Not handled)");
    }
  }
  else
    D("Entity doesn't have an acquire-function");
  D("Returning specific free entity");
  return temp->entity;
}
/* Get a free entity from the branch			*/
inline void* get_free(struct entity_list_branch *br,void * opt,void* acq, int* acquire_result)
{
  LOCK(&(br->branchlock));
  while(br->freelist == NULL){
    if(br->busylist == NULL && br->loadedlist == NULL){
      D("No entities in list. Returning NULL");
      UNLOCK(&(br->branchlock));
      return NULL;
    }
    D("Failed to get free buffer. Sleeping");
    pthread_cond_wait(&(br->busysignal), &(br->branchlock));
  }
  struct listed_entity * temp = br->freelist;
  mutex_free_set_busy(br, br->freelist);
  UNLOCK(&(br->branchlock));
  if(temp->acquire !=NULL){
    D("Running acquire on entity");
    int ret = temp->acquire(temp->entity, opt,acq);
    if(acquire_result != NULL)
      *acquire_result = ret;
    else{
      if(ret != 0)
	E("Acquire return non-zero value(Not handled)");
    }
  }
  else
    D("Entity doesn't have an acquire-function");
  return temp->entity;
}
/* Set this entity as busy in this branch		*/
inline void set_busy(struct entity_list_branch *br, struct listed_entity* en)
{
  LOCK(&(br->branchlock));
  mutex_free_set_busy(br,en);
  UNLOCK(&(br->branchlock));
}
void print_br_stats(struct entity_list_branch *br){
  int free=0,busy=0,loaded=0;
  LOCK(&(br->branchlock));
  struct listed_entity *le = br->freelist;
  while(le != NULL){
    free++;
    le = le->child;
  }
  le = br->busylist;
  while(le != NULL){
    busy++;
    le = le->child;
  }
  le = br->loadedlist;
  while(le != NULL){
    loaded++;
    le = le->child;
  }
  UNLOCK(&(br->branchlock));
  LOG("Free:\t%d\tBusy:\t%d\tLoaded:\t%d\n", free, busy, loaded);
}
/* Loop through all entities and do specified OP */
/* Don't want to write this same thing 4 times , so I'll just add an operation switch */
/* for it */
void oper_to_list(struct entity_list_branch *br,struct listed_entity *le, int operation, void*param){
  struct listed_entity * removable = NULL;
  //struct buffer_entity *be;
  while(le != NULL){
    switch(operation){
      case BRANCHOP_STOPANDSIGNAL:
	((struct buffer_entity*)le->entity)->stop((struct buffer_entity*)le->entity);
	break;
      case BRANCHOP_GETSTATS:
	get_io_stats(((struct recording_entity*)(le->entity))->opt, (struct stats*)param);
	break;
      case BRANCHOP_CLOSERBUF:
	((struct buffer_entity*)le->entity)->close(((struct buffer_entity*)le->entity), param);
	removable = le;
	break;
      case BRANCHOP_CLOSEWRITER:
	D("Closing writer");
	((struct recording_entity*)le->entity)->close(((struct recording_entity*)le->entity),param);
	removable = le;
	//D("Writer closed");
	break;
      case BRANCHOP_WRITE_CFGS:
	D("Writing cfg");
	((struct recording_entity*)le->entity)->writecfg(((struct recording_entity*)le->entity), param);
	break;
      case BRANCHOP_READ_CFGS:
	((struct recording_entity*)le->entity)->readcfg(((struct recording_entity*)le->entity), param);
	break;
      case BRANCHOP_CHECK_FILES:
	((struct recording_entity*)le->entity)->check_files(((struct recording_entity*)le->entity), param);
	break;
    }
    le = le->child;
    if(removable != NULL){
      remove_from_branch(br,removable,1);
      //free(removable);
    }
  }
}
void oper_to_all(struct entity_list_branch *br, int operation,void* param)
{
  LOCK(&(br->branchlock));
  oper_to_list(br,br->freelist,operation,param);
  oper_to_list(br,br->busylist,operation, param);
  oper_to_list(br,br->loadedlist,operation, param);
  UNLOCK(&(br->branchlock));
}
