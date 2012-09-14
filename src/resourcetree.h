#ifndef RESOURCETREE_H
#define RESOURCETREE_H

#define CHECK_BY_NAME 1
#define CHECK_BY_OPTPOINTER 2
#define CHECK_BY_IDSTRING 3
#define CHECK_BY_NOTFOUND 4
#define CHECK_BY_SEQ 5
#define CHECK_BY_FINISHED 6
#define CHECK_BY_OLDSEQ 7
#if(SPINLOCK)
#define LOCKTYPE pthread_spinlock_t
#define LOCK(x) pthread_spin_lock(x)
#define UNLOCK(x) pthread_spin_unlock(x)
#define LOCK_INIT(x) pthread_spin_init((x), PTHREAD_PROCESS_SHARED)
#define LOCK_DESTROY(x) pthread_spin_destroy(x)
#define LOCK_FREE(x) (void)x
#else
#define LOCKTYPE pthread_mutex_t
#define LOCK(x) pthread_mutex_lock(x)
#define UNLOCK(x) pthread_mutex_unlock(x)
#define LOCK_INIT(x) pthread_mutex_init((x), NULL)
#define LOCK_DESTROY(x) pthread_mutex_destroy(x)
#define LOCK_FREE(x) free(x)
#endif
//#include <pthread.h>

/* This holds any entity, which can be set to either 	*/
/* A branches free or busy-list				*/
/* Cant decide if rather traverse the lists and remove	*/
/* father or keep it as is, to avoid traversing		*/
struct listed_entity
{
  struct listed_entity* child;
  struct listed_entity* father;
  int (*acquire)(void*,void*,void*);
  int (*check)(void*, int);
  /* 0 for not this, 1 for identified */
  int (*identify)(void*, void*, void*,int);
  void (*infostring)(void*, char*);
  int (*close)(void*);
  int (*release)(void*);
  void* entity;
};
/* Holds all the listed_entits of a common type		*/
/* The idea is to manipulate and request entities from 	*/
/* this branch						*/
struct entity_list_branch
{
  struct listed_entity *freelist;
  struct listed_entity *busylist;
  struct listed_entity *loadedlist;
  int mutex_free;
  LOCKTYPE branchlock;
  /* Added here so the get_free caller can sleep	*/
  /* On non-free branch					*/
  pthread_cond_t busysignal;
};
/* Initial add */
void add_to_entlist(struct entity_list_branch* br, struct listed_entity* en);
/* Set this entity into the free to use list		*/
void set_free(struct entity_list_branch *br, struct listed_entity* en);
/* Set branch as full of data				*/
void set_loaded(struct entity_list_branch *br, struct listed_entity* en);
/* Get a free entity from the branch			*/
void* get_free(struct entity_list_branch *br, void * opt,void *acq, int* acquire_result);
/* Get a file thats still in the memory and is free */
void* get_lingering(struct entity_list_branch * br, void* opt, void*  fh, int just_check);
/* Get a specific entity according to seq or bufnum	*/
void* get_specific(struct entity_list_branch *br, void * opt,unsigned long seq, unsigned long bufnum, unsigned long id, int* acquire_result);
/* Get a loaded buffer according to seq. Block if not found	*/
void* get_loaded(struct entity_list_branch *br, unsigned long seq, void* opt);
void remove_from_branch(struct entity_list_branch *br, struct listed_entity *en, int mutex_free);
/* Set this entity as busy in this branch		*/
void set_busy(struct entity_list_branch *br, struct listed_entity* en);
/* Do an operation to all members of a branch		*/
void oper_to_all(struct entity_list_branch *be,int operation ,void* param);
/* Print stats on how many entities are free, busy or loaded	*/
void print_br_stats(struct entity_list_branch *br);
/* Blocks until no more entities are busy with this element	*/
void block_until_free(struct entity_list_branch *br, void* val1);
struct listed_entity* get_from_all(struct entity_list_branch *br, void *val1, void * val2, int iden_type, int mutex_free);
struct listed_entity * loop_and_check(struct listed_entity* head, void* val1, void* val2, int iden_type);

void mutex_free_change_branch(struct listed_entity **from, struct listed_entity **to, struct listed_entity *en);
#endif /* !RESOURCETREE_H */
