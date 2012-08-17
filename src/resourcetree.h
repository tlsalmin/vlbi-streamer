#ifndef RESOURCETREE_H
#define RESOURCETREE_H
/* This holds any entity, which can be set to either 	*/
/* A branches free or busy-list				*/
/* Cant decide if rather traverse the lists and remove	*/
/* father or keep it as is, to avoid traversing		*/
struct listed_entity
{
  struct listed_entity* child;
  struct listed_entity* father;
  int (*acquire)(void*,void*,unsigned long,unsigned long);
  int (*check)(void*, int);
  const char* (*getrecname)(void*);
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
  pthread_mutex_t branchlock;
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
void* get_free(struct entity_list_branch *br, void * opt,unsigned long seq, unsigned long bufnum, int* acquire_result);
/* Get a specific entity according to seq or bufnum	*/
void* get_specific(struct entity_list_branch *br, void * opt,unsigned long seq, unsigned long bufnum, unsigned long id, int* acquire_result);
/* Get a loaded buffer according to seq. Block if not found	*/
void* get_loaded(struct entity_list_branch *br, unsigned long seq);
void remove_from_branch(struct entity_list_branch *br, struct listed_entity *en, int mutex_free);
/* Set this entity as busy in this branch		*/
void set_busy(struct entity_list_branch *br, struct listed_entity* en);
/* Do an operation to all members of a branch		*/
void oper_to_all(struct entity_list_branch *be,int operation ,void* param);
/* Print stats on how many entities are free, busy or loaded	*/
void print_br_stats(struct entity_list_branch *br);
/* Blocks until no more entities are busy with this element	*/
void block_until_free(struct entity_list_branch *br, const char* recname);

#endif /* !RESOURCETREE_H */
