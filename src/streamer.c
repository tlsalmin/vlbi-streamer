#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/resource.h> /*Query max allocatable memory */
//TODO: Add explanations for includes
#include <netdb.h> // struct hostent
#include <time.h>
#include "config.h"
#include "streamer.h"
#include "fanout.h"
#include "udp_stream.h"
//#include "ringbuf.h"
#ifdef HAVE_LIBAIO
#include "aiowriter.h"
#endif
#include "common_wrt.h"
#include "defwriter.h"
//#include "sendfile_streamer.h"
#include "splicewriter.h"
#include "simplebuffer.h"
#define IF_DUPLICATE_CFG_ONLY_UPDATE
#define KB 1024
/* from http://stackoverflow.com/questions/1076714/max-length-for-client-ip-address */
/* added one for null char */
#define IP_LENGTH 46
#define BYTES_TO_MBITSPS(x)	(x*8)/(KB*KB)
/* Segfaults if pthread_joins done at exit. Tried to debug this 	*/
/* for days but no solution						*/
//#define UGLY_FIX_ON_RBUFTHREAD_EXIT
//TODO: Search these
#define MAX_PRIO_FOR_PTHREAD 4
#define MIN_PRIO_FOR_PTHREAD 1
#if(DAEMON)
#define STREAMER_CHECK_NONNULL(val,mes) do{if(val==NULL){perror(mes);E(mes);pthread_exit(NULL);}else{D(mes);}}while(0)
#else
#define STREAMER_CHECK_NONNULL(val,mes) do{if(val==NULL){perror(mes);E(mes);return -1;}else{D(mes);}}while(0)
#endif
#if(DAEMON)
#define STREAMER_EXIT opt->status = STATUS_FINISHED; pthread_exit(NULL)
#define STREAMER_ERROR_EXIT opt->status = STATUS_ERROR; pthread_exit(NULL)
#else
#define STREAMER_EXIT return 0
#define STREAMER_ERROR_EXIT return -1
#endif


#define BRANCHOP_STOPANDSIGNAL 1
#define BRANCHOP_GETSTATS 2
#define BRANCHOP_CLOSERBUF 3
#define BRANCHOP_CLOSEWRITER 4
#define BRANCHOP_WRITE_CFGS 5
#define BRANCHOP_READ_CFGS 6
#define BRANCHOP_CHECK_FILES 7

#ifdef PRIORITY_SETTINGS
#define FREE_AND_ERROREXIT if(opt->device_name != NULL){free(opt->device_name);} if(opt->optbits & READMODE){ if(opt->fileholders != NULL) free(opt->fileholders); } config_destroy(&(opt->cfg)); free(opt->membranch); free(opt->diskbranch); pthread_attr_destroy(&pta);exit(-1);
#else
#define FREE_AND_ERROREXIT if(opt->device_name != NULL){free(opt->device_name);} if(opt->optbits & READMODE){ if(opt->fileholders != NULL) free(opt->fileholders); } config_destroy(&(opt->cfg)); free(opt->membranch); free(opt->diskbranch); exit(-1);
#endif
/* This should be more configurable */
extern char *optarg;
extern int optind, optopt;

//static struct opt_s opt;
void specadd(struct timespec * to, struct timespec *from){
  if(to->tv_nsec + from->tv_nsec >  BILLION){
    to->tv_sec++;
    to->tv_nsec += (BILLION-to->tv_nsec)+from->tv_nsec;
  }
  else{
    to->tv_nsec+=from->tv_nsec;
    to->tv_sec+=from->tv_sec;
  }
}
/* Return the diff of the two timespecs in nanoseconds */
long nanodiff(TIMERTYPE * start, TIMERTYPE *end){
  unsigned long temp=0;
  temp += (end->tv_sec-start->tv_sec)*BILLION;
#ifdef TIMERTYPE_GETTIMEOFDAY
  temp += (end->tv_usec-start->tv_usec)*1000;
#else
  temp += end->tv_nsec-start->tv_nsec;
#endif
  return temp;
}
void nanoadd(TIMERTYPE * datime, unsigned long nanos_to_add){
#ifdef TIMERTYPE_GETTIMEOFDAY
  if(datime->tv_usec*1000 + nanos_to_add > BILLION)
#else
  if(datime->tv_nsec + nanos_to_add >  BILLION)
#endif
  {
    datime->tv_sec++;
#ifdef TIMERTYPE_GETTIMEOFDAY
    datime->tv_usec += (MILLION-datime->tv_usec)+nanos_to_add/1000;
#else
    datime->tv_nsec += (BILLION-datime->tv_nsec)+nanos_to_add;
#endif
  }
  else
  {
#ifdef TIMERTYPE_GETTIMEOFDAY
    datime->tv_usec += nanos_to_add/1000;
#else
    datime->tv_nsec += nanos_to_add;
#endif
  }
}
int get_sec_diff(TIMERTYPE *timenow, TIMERTYPE* event){
  int diff = 0;
  /* Straight second diff */
  diff += event->tv_sec - timenow->tv_sec;
#ifdef TIMERTYPE_GETTIMEOFDAY
  diff += (event->tv_usec-timenow->tv_usec)/1000;
#else
  diff += (event->tv_nsec-timenow->tv_nsec)/MILLION;
#endif
  return diff;
}
void zeroandadd(TIMERTYPE *datime, unsigned long nanos_to_add){
  /*
  datime->tv_sec = 0;
  datime->tv_nsec = 0;
  */
  ZEROTIME((*datime));
  nanoadd(datime,nanos_to_add);
}

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
    toadd->child = NULL;
    (*root)->child= toadd;
  }
}
/* Initial add */
void add_to_entlist(struct entity_list_branch* br, struct listed_entity* en)
{
  pthread_mutex_lock(&(br->branchlock));
  add_to_next(&(br->freelist), en);
  pthread_mutex_unlock(&(br->branchlock));
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
  pthread_mutex_lock(&(br->branchlock));
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
  pthread_mutex_unlock(&(br->branchlock));
}
void set_loaded(struct entity_list_branch *br, struct listed_entity* en){
  D("Setting entity to loaded");
  pthread_mutex_lock(&(br->branchlock));
  mutex_free_change_branch(&(br->busylist), &(br->loadedlist), en);
  pthread_cond_broadcast(&(br->busysignal));
  pthread_mutex_unlock(&(br->branchlock));
}
void mutex_free_set_busy(struct entity_list_branch *br, struct listed_entity* en)
{
  mutex_free_change_branch(&(br->freelist),&(br->busylist), en);
}
void remove_from_branch(struct entity_list_branch *br, struct listed_entity *en, int mutex_free){
  D("Removing entity from branch");
  if(!mutex_free){
    pthread_mutex_lock(&(br->branchlock));
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
  else{
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
    pthread_mutex_unlock(&(br->branchlock));
  }
  D("Entity removed from branch");
}
inline struct listed_entity * loop_and_check(struct listed_entity* head, int seq){
  while(head != NULL){
    if(head->check(head->entity, seq) == 1){
      return head;
    }
    else{
      head = head->child;
    }
  }
  return NULL;
}
struct listed_entity* get_w_check(struct listed_entity **lep, int seq, struct listed_entity **other, struct listed_entity **other2, struct entity_list_branch* br){
  //struct listed_entity *temp;
  struct listed_entity *le = NULL;
  //le = NULL;
  while(le== NULL){
    le = loop_and_check(*lep, seq);
    /* If le wasn't found in the list */
    if(le == NULL){
      /* Check if branch is dead */
      D("Checking for dead branch");
      if(*lep == NULL && *other == NULL && *other2 == NULL){
	E("No entities in list. Returning NULL");
	return NULL;
      }
      /* Need to check if it exists at all */
      D("Looping to check if exists");
      if(loop_and_check(*other, seq) == NULL && loop_and_check(*other2,seq) == NULL){
	D("Rec point disappeared!");
	return NULL;
      }
      D("Failed to get free buffer. Sleeping waiting for %d",,seq);
      pthread_cond_wait(&(br->busysignal), &(br->branchlock));
      D("Woke up! Checking for %d again",, seq);
    }
  }
  D("Found specific elem id %d!",, seq);
  return le;
}
/* Get a loaded buffer with the specific seq */
inline void* get_loaded(struct entity_list_branch *br, unsigned long seq){
  D("Querying for loaded entity");
  pthread_mutex_lock(&(br->branchlock));
  struct listed_entity * temp = get_w_check(&br->loadedlist, seq, &br->freelist, &br->busylist, br);

  if (temp == NULL){
    pthread_mutex_unlock(&(br->branchlock));
    return NULL;
  }

  mutex_free_change_branch(&(br->loadedlist), &(br->busylist), temp);
  pthread_mutex_unlock(&(br->branchlock));
  D("Returning loaded entity");
  return temp->entity;
}
/* Get a specific free entity from branch 		*/
inline void* get_specific(struct entity_list_branch *br,void * opt,unsigned long seq, unsigned long bufnum, unsigned long id, int* acquire_result)
{
  pthread_mutex_lock(&(br->branchlock));
  struct listed_entity* temp = get_w_check(&br->freelist, id, &br->busylist, &br->loadedlist, br);
  
  if(temp ==NULL){
    pthread_mutex_unlock(&(br->branchlock));
    if(acquire_result !=NULL)
      *acquire_result = -1;
    return NULL;
  }

  mutex_free_change_branch(&(br->freelist), &(br->busylist), temp);
  pthread_mutex_unlock(&(br->branchlock));
  if(temp->acquire !=NULL){
    D("Running acquire on entity");
    int ret = temp->acquire(temp->entity, opt,seq, bufnum);
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
inline void* get_free(struct entity_list_branch *br,void * opt,unsigned long seq, unsigned long bufnum, int* acquire_result)
{
  pthread_mutex_lock(&(br->branchlock));
  while(br->freelist == NULL){
    if(br->busylist == NULL && br->loadedlist == NULL){
      D("No entities in list. Returning NULL");
      pthread_mutex_unlock(&(br->branchlock));
      return NULL;
    }
    D("Failed to get free buffer. Sleeping");
    pthread_cond_wait(&(br->busysignal), &(br->branchlock));
  }
  struct listed_entity * temp = br->freelist;
  mutex_free_set_busy(br, br->freelist);
  pthread_mutex_unlock(&(br->branchlock));
  if(temp->acquire !=NULL){
    D("Running acquire on entity");
    int ret = temp->acquire(temp->entity, opt,seq, bufnum);
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
  pthread_mutex_lock(&(br->branchlock));
  mutex_free_set_busy(br,en);
  pthread_mutex_unlock(&(br->branchlock));
}
void print_br_stats(struct entity_list_branch *br){
  int free=0,busy=0,loaded=0;
  pthread_mutex_lock(&(br->branchlock));
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
  pthread_mutex_unlock(&(br->branchlock));
  LOG("Free: %d, Busy: %d, Loaded: %d\n", free, busy, loaded);
}
int write_cfgs_to_disks(struct opt_s *opt){
  if(opt->optbits & READMODE)
    oper_to_all(opt->diskbranch, BRANCHOP_READ_CFGS, opt);
  else
    oper_to_all(opt->diskbranch, BRANCHOP_WRITE_CFGS, opt);
  return 0;
}
#ifdef HAVE_LIBCONFIG_H
/* Set all the variables to opt from root. If check is set, then just	*/
/* check the variables against options in opt and return -1 if there	*/
/* is a discrepancy. If write is 1, the option is written to the cfg	*/
/* instead for being read from the cfg					*/
int set_from_root(struct opt_s * opt, config_setting_t *root, int check, int write){
  D("Option root parse, check: %d, write %d",,check,write);
  config_setting_t * setting;
  int err=0,index=0,filesize_found=0;
  unsigned long filesize;
  /* If no root specified, use opt->cfg root */
  if(root == NULL)
    root = config_root_setting(&(opt->cfg));

  setting = config_setting_get_elem(root,index);

  while(setting != NULL){
    /* Have to make this a huge if else since its string matching */
    if(strcmp(config_setting_name(setting), "filesize") == 0){
      if(config_setting_type(setting) != CONFIG_TYPE_INT64){
	E("Filesize not int64");
	return -1;
      }
      /* Check for same filesize. Now this loops has to have been performed	*/
      /* once before we try to check the option due to needing packet_size	*/
      else if(check == 1){
	if((unsigned long)config_setting_get_int64(setting) != opt->packet_size*opt->buf_num_elems)
	  return -1;
      }
      else if(write == 1){
	filesize = opt->packet_size*opt->buf_num_elems;
	err = config_setting_set_int64(setting,filesize);
	if(err != CONFIG_TRUE){
	  E("Writing filesize: %d",, err);
	  return -1;
	}
      }
      else{
	filesize_found=1;
	filesize = (unsigned long)config_setting_get_int64(setting);
      }
    }
    /* "Legacy" stuff*/
    CFG_ELIF("buf_elem_size"){
      if(config_setting_type(setting) != CONFIG_TYPE_INT64)
	return -1;
      if(check==1){
	if(((unsigned long)config_setting_get_int64(setting)) != opt->packet_size)
	  return -1;
      }
      else if(write==1){
	if(((unsigned long)config_setting_get_int64(setting)) != opt->packet_size)
	  return -1;
      }
      else
	opt->packet_size = (unsigned long)config_setting_get_int64(setting);
    }
    CFG_FULL_STR(filename)
    CFG_FULL_UINT64(opt->cumul,"cumul")
    CFG_FULL_STR(device_name)
    CFG_FULL_UINT64(opt->optbits, "optbits")
    CFG_FULL_UINT64(opt->time, "time")
    CFG_FULL_INT(opt->port, "port")
    CFG_FULL_UINT64(opt->minmem, "minmem")
    CFG_FULL_UINT64(opt->maxmem, "maxmem")
    CFG_FULL_INT(opt->n_threads, "n_threads")
    CFG_FULL_INT(opt->n_drives, "n_drives")
    CFG_FULL_INT(opt->rate, "rate")
    CFG_FULL_UINT64(opt->do_w_stuff_every, "do_w_stuff_every")
    CFG_FULL_INT(opt->wait_nanoseconds, "wait_nanoseconds")
    CFG_FULL_UINT64(opt->packet_size, "packet_size")
    CFG_FULL_INT(opt->buf_num_elems, "buf_num_elems")
    CFG_FULL_INT(opt->buf_division, "buf_division")
    CFG_FULL_STR(hostname)
    CFG_FULL_UINT64(opt->serverip, "serverip")
    CFG_FULL_UINT64(opt->total_packets, "total_packets")

    setting = config_setting_get_elem(root,++index);
  }
  /* Only use filesize here, since it needs packet_size */
  if(filesize_found==1){
    opt->buf_num_elems = filesize/opt->packet_size;
    opt->do_w_stuff_every = filesize/((unsigned long)opt->buf_division);
  }
  
  return 0;
}
/* Init a rec cfg */
int stub_rec_cfg(config_setting_t *root){
  config_setting_t *setting;
  setting = config_setting_add(root, "packet_size", CONFIG_TYPE_INT64);
  CHECK_ERR_NONNULL(setting, "add packet_size");
  setting = config_setting_add(root, "cumul", CONFIG_TYPE_INT64);
  CHECK_ERR_NONNULL(setting, "add cumul");
  /* If we're using the simpler buffer calculation, which fixes the 	*/
  /* size of the files, we don't need filesize etc. here anymore	*/
#ifndef SIMPLE_BUFCACL
  setting = config_setting_add(root, "filesize", CONFIG_TYPE_INT64);
  CHECK_ERR_NONNULL(setting, "add filesize");
#endif
  setting = config_setting_add(root, "total_packets", CONFIG_TYPE_INT64);
  CHECK_ERR_NONNULL(setting, "add total_packets");
#ifndef SIMPLE_BUFCACL
  setting = config_setting_add(root, "buf_division", CONFIG_TYPE_INT);
  CHECK_ERR_NONNULL(setting, "add buf_division");
#endif
  return 0;
}
int stub_full_cfg(config_setting_t *root){
  config_setting_t *setting;
  //stub_rec_cfg(root);
  CFG_ADD_INT64(packet_size);
  CFG_ADD_INT64(optbits);
  CFG_ADD_STR(device_name);
  CFG_ADD_INT64(time);
  CFG_ADD_INT(port);
  CFG_ADD_INT64(minmem);
  CFG_ADD_INT64(maxmem);
  CFG_ADD_INT(n_threads);
  CFG_ADD_INT(n_drives);
  CFG_ADD_INT(rate);
  CFG_ADD_INT64(do_w_stuff_every);
  CFG_ADD_INT64(wait_nanoseconds);
  CFG_ADD_STR(hostname);
  CFG_ADD_INT64(serverip);
  return 0;
}
/* Combination of full and session specific conf */
int stub_full_log_cfg(config_setting_t *root){
  stub_rec_cfg(root);
  stub_full_cfg(root);
  return 0;
}
int read_full_cfg(struct opt_s *opt){
  int err;
  if(opt->cfgfile == NULL)
    return -1;
  err = read_cfg(&(opt->cfg),opt->cfgfile);
  CHECK_ERR("Read cfg");
  return set_from_root(opt,NULL,0,0);
}
/* The full_cfg format is a bit different than the init_cfg format	*/
/* Full cfgs have a root element for a session name to distinguish them	*/
/* from one another.							*/
int write_full_cfg(struct opt_s *opt){
  config_setting_t *root, *setting;
  int err=0;
  D("Initializing CFG");
  config_init(&(opt->cfg));
  //err = config_read_file(&(opt->cfg), opt->cfgfile);
  //CHECK_CFG("Load config");
  root = config_root_setting(&(opt->cfg));
  CHECK_ERR_NONNULL(root, "Get root");

  /* Check if cfg already exists					*/
  setting = config_lookup(&(opt->cfg), opt->filename);
  if(setting != NULL){
    LOG("Configlog for %s already present!", opt->filename);
#ifdef IF_DUPLICATE_CFG_ONLY_UPDATE
    LOG("Only updating configlog");
    err = config_setting_remove(root, opt->filename);
#else
#endif
  }
  
  /* Since were writing, we should check if a cfg  group with the same 	*/
  /* name already exists						*/
  setting = config_setting_add(root, opt->filename, CONFIG_TYPE_GROUP);
  CALL_AND_CHECK(stub_full_cfg, setting);
  CALL_AND_CHECK(set_from_root, opt,setting,0,1);
  return 0;
}
/* TODO: Move this to the disk init */
int init_cfg(struct opt_s *opt){
  config_setting_t *root;
  int err=0;
  int i;
  D("Initializing CFG");

  if(opt->optbits & READMODE){
    int found = 0;
    /* For checking on cfg-file consistency */
    //long long packet_size=0,cumul=0,old_packet_size=0,old_cumul=0;//,n_files=0,old_n_files=0;
    char * path = (char*) malloc(sizeof(char)*FILENAME_MAX);
    CHECK_ERR_NONNULL(path, "Filepath malloc");
    for(i=0;i<opt->n_drives;i++){
      sprintf(path, "%s%s%s", opt->filenames[i],opt->filename ,".cfg");
      if(! config_read_file(&(opt->cfg),path)){
	E("%s:%d - %s",, path, config_error_line(&opt->cfg), config_error_text(&opt->cfg));
      }
      else{
	LOG("Config found on %s\n",path); 
	root = config_root_setting(&(opt->cfg));
	if(found == 0){
	  set_from_root(opt,root,0,0);
	  found = 1;
	  D("Getting opts from first config, cumul is %lu",, opt->cumul);
	  opt->fileholders = (int*)malloc(sizeof(int)*(opt->cumul));
	  CHECK_ERR_NONNULL(opt->fileholders, "fileholders malloc");
	  //memset(opt->fileholders, -1,sizeof(int)*(opt->cumul));
	  int j;
	  for(j=0;(unsigned)j<opt->cumul;j++)
	    opt->fileholders[j] = -1;
	  D("opts read from first config");
	}
	else{
	  err = set_from_root(opt,root,1,0);
	  if( err != 0)
	    E("Discrepancy in config at %s",, path);
	  else
	    D("Config at %s conforms to previous",, path);
	}
      }
    }
    if(found == 0){
      E("No config file found! This means no recording with said name found");
      return -1;
    }
    else{
      LOG("Config file reading finished\n");
    }
    free(path);
    //config_destroy(&opt->cfg);
  }
  else{
    /* Set the root and other settings we need */
    root = config_root_setting(&(opt->cfg));
    stub_rec_cfg(root);
  }
  return 0;
}
#endif
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
	((struct recording_entity*)le->entity)->check_files(((struct recording_entity*)le->entity));
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
  pthread_mutex_lock(&(br->branchlock));
  oper_to_list(br,br->freelist,operation,param);
  oper_to_list(br,br->busylist,operation, param);
  oper_to_list(br,br->loadedlist,operation, param);
  pthread_mutex_unlock(&(br->branchlock));
}
int remove_specific_from_fileholders(struct opt_s *opt, int id){
  unsigned int i;
  for(i=0; i < opt->cumul ;i++){
    if(opt->fileholders[i] == id)
      opt->fileholders[i] = -1;
  }
  return 0;
}
int calculate_buffer_sizes(struct opt_s *opt){
  /* Calc how many elementes we get into the buffer to fill the minimun */
  /* amount of memory we want to use					*/

  /* Magic is the n of blocks we wan't to divide the ringbuffer to	*/
  opt->buf_division = 8;
  //unsigned long bufsize;// = opt.packet_size;
  int found = 0;

  int extra= 0;
  if(opt->optbits & USE_RX_RING){
    while((opt->packet_size %16)!= 0){
      if(opt->optbits  &READMODE)
	E("Shouldn't need this in sending with RX-ring!");
      opt->packet_size++;
      extra++;
    }
    D("While using RX-ring we need to reserve %d extra bytes per buffer element",, extra);
  }

  /* TODO: do_w_stuff gets warped  from MB to num of elems*/
  LOG("STREAMER: Calculating total buffer size between "
      "%lu GB to %luGB,"
      " size %lu packets, "
      "Doing maximum %luMB size writes\n"
      ,opt->minmem, opt->maxmem, opt->packet_size, opt->do_w_stuff_every/MEG);
  /* Set do_w_stuff to minimum wanted */
  /* First set do_w_stuff to be packet aligned */
  unsigned long temp = opt->do_w_stuff_every/opt->packet_size;
  LOG("%lu\n",temp);
  opt->do_w_stuff_every = temp*(opt->packet_size);

  /* Increase block division to fill min amount of memory */
  while((opt->do_w_stuff_every)*opt->buf_division*(opt->n_threads) < (opt->minmem)*GIG){
    opt->buf_division++;
  }
  /* Store for later use if proper size not found with current opt->buf_division */
  temp = opt->do_w_stuff_every;
  while((found == 0) && (opt->buf_division > 0)){
    /* Increase buffer size until its BLOCK_ALIGNed */
    while((opt->do_w_stuff_every)*opt->buf_division*(opt->n_threads) < (opt->maxmem)*GIG){
      if(opt->do_w_stuff_every % BLOCK_ALIGN == 0){
	found=1;
	opt->buf_num_elems = (opt->do_w_stuff_every*opt->buf_division)/opt->packet_size;
	//opt->do_w_stuff_every = opt->do_w_stuff_every/opt->packet_size;
	break;
      }
      opt->do_w_stuff_every+=opt->packet_size;
    }
    if(found == 0){
      opt->do_w_stuff_every = temp;
      opt->buf_division--;
    }
  }
  if(found ==0){
    LOGERR("STREAMER: Didnt find Alignment"
	"%lu GB to %luGB"
	", Each buffer having %lu bytes"
	", Writing in %lu size blocks"
	", %d Blocks per buffer"
	", Elements in buffer %d\n"
	,opt->minmem, opt->maxmem, opt->packet_size*(opt->buf_num_elems), opt->do_w_stuff_every,opt->buf_division ,opt->buf_num_elems);
    //LOG("STREAMER: Didnt find alignment for %lu on %d threads, with w_every %lu\n", opt->packet_size,opt->n_threads, (opt->packet_size*(opt->buf_num_elems))/opt->buf_division);
    return -1;
  }
  else{
    if(opt->optbits & USE_RX_RING){
      D("The 16 aligned restriction of RX-ring resulted in %d MB larger memory use",, extra*opt->buf_num_elems*opt->n_threads/MEG);
    }

    /*
       long filesztemp =0;
       while(filesztemp < opt.filesize)
       filesztemp+=opt.do_w_stuff_every;
       opt.filesize= filesztemp;
       */
    //opt.filesize = opt->buf_num_elems*(opt->packet_size);

    LOG("STREAMER: Alignment found between "
	"%lu GB to %luGB"
	", Each buffer having %lu MB"
	", Writing in %lu MB size blocks"
	", Elements in buffer %d"
	", Total used memory: %luMB\n"
	,opt->minmem, opt->maxmem, (opt->packet_size*(opt->buf_num_elems))/MEG, (opt->do_w_stuff_every)/MEG, opt->buf_num_elems, (opt->buf_num_elems*opt->packet_size*opt->n_threads)/MEG);
    //LOG("STREAMER: Alignment found for %lu size packet with %d threads at %lu with ringbuf in %lu blocks. hd write size as %lu\n", opt->packet_size,opt->n_threads ,opt->buf_num_elems*(opt->packet_size),opt->buf_division, (opt->buf_num_elems*opt->packet_size)/opt->buf_division);
    return 0;
  }
}
int calculate_buffer_sizes_simple(struct opt_s * opt){ 
  /* A very simple buffer size calculator that fixes the filesizes to 	*/
  /* constants according to the packet size. We try to keep the		*/
  /*filesize between 256 and 512 and must keep it packet- and 		*/
  /* blockaligned at all times						*/
  int extra= 0;
  if(opt->optbits & USE_RX_RING){
    while((opt->packet_size %16)!= 0){
      if(opt->optbits  &READMODE)
	E("Shouldn't need this in sending with RX-ring!");
      opt->packet_size++;
      extra++;
    }
    D("While using RX-ring we need to reserve %d extra bytes per buffer element",, extra);
  }
  opt->buf_division = B(3);
  while(opt->packet_size*BLOCK_ALIGN*opt->buf_division >= 512*MEG)
    opt->buf_division  >>= 1;
  while(opt->packet_size*BLOCK_ALIGN*opt->buf_division <= 256*MEG)
    opt->buf_division  <<= 1;
  opt->buf_num_elems = (BLOCK_ALIGN*opt->buf_division);
  opt->do_w_stuff_every = (BLOCK_ALIGN*opt->packet_size);

  opt->n_threads = 1;
  while(opt->n_threads*opt->packet_size*opt->buf_num_elems < opt->maxmem*GIG)
    opt->n_threads++;
  opt->n_threads -=1;

  return 0;
}
/*
 * Adapted from http://coding.debuntu.org/c-linux-socket-programming-tcp-simple-http-client
 */
/*
   int resolve_host(char *host, struct in_addr * ia){
   int err=0;
   return err;
   }
   */

/*
 * Stuff stolen from lindis sendfileudp
 */
static void usage(char *binary){
  LOGERR(
      "usage: %s [OPTIONS]... name (time to receive / host to send to)\n"
      "-A MAXMEM	Use maximum MAXMEM amount of memory for ringbuffers(default 12GB)\n"
#ifdef HAVE_RATELIMITER
      "-a MYY		Wait MYY microseconds between packet sends\n"
#endif
      "-t {fanout|udpstream|sendfile|TODO	Capture type(Default: udpstream)(sendfile is a prototype not yet in kernel)(fanout doesn't write to disk. Poor performance)\n"
      "-c CFGFILE	Load config from cfg-file CFGFILE\n"
      //"-a {lb|hash}	Fanout type(Default: lb)\n"
      "-d DRIVES	Number of drives(Default: 1)\n"
      "-i INTERFACE	Which interface to bind to(Not required)\n"
      "-I MINMEM	Use at least MINMEM amount of memory for ringbuffers(default 4GB)\n"
      "-m {s|r}		Send or Receive the data(Default: receive)\n"
      "-n NUM	        Number of threads(Default: DRIVES+2)\n"
      "-p SIZE		Set buffer element size to SIZE(Needs to be aligned with sent packet size)\n"
#ifdef CHECK_OUT_OF_ORDER
      "-q 		Check if packets are in order from first 64bits of package(Not yet implemented)\n"
#endif
      "-r RATE		Expected network rate in MB(default: 10000)(Deprecated)\n"
      "-s SOCKET	Socket number(Default: 2222)\n"
#ifdef HAVE_HUGEPAGES
      "-u 		Use hugepages\n"
#endif
      "-v 		Verbose. Print stats on all transfers\n"
      "-V 		Verbose. Print stats on individual mountpoint transfers\n"
      "-W WRITEEVERY	Try to do HD-writes every WRITEEVERY MB(default 16MB)\n"
      "-w {"
#ifdef HAVE_LIBAIO
      "aio|"
#endif
      "def|splice|dummy}	Choose writer to use(Default: def)\n"
      "-x 		Use an mmap rxring for receiving\n"
      ,binary);
}
/* Why don't I just memset? */
void init_stats(struct stats *stats){
  //memset(stats, 0,sizeof(stats));
  stats->total_bytes = 0;
  stats->incomplete = 0;
  stats->total_written = 0;
  stats->total_packets = 0;
  stats->dropped = 0;
}
void neg_stats(struct stats* st1, struct stats* st2){
  /* We sometimes get a situation, where the previous val is larger 	*/
  /* than the new value. This shouldn't happen! For now I'll just add	*/
  /* an ugly hack here. TODO: Solve					*/
  /* NOTE: This doesn't affect the final stats				*/
#ifdef UGLY_HACKS_ON_STATS
  if(st1->total_bytes < st2->total_bytes)
    st1->total_bytes =0 ;
  else
#endif
    st1->total_bytes -= st2->total_bytes;
  st1->incomplete -= st2->incomplete;
#ifdef UGLY_HACKS_ON_STATS
  if(st1->total_written < st2->total_written)
    st1->total_written =0 ;
  else
#endif
    st1->total_written -= st2->total_written;
#ifdef UGLY_HACKS_ON_STATS
  if(st1->total_packets < st2->total_packets)
    st1->total_packets =0 ;
  else
#endif
    st1->total_packets -= st2->total_packets;
  st1->dropped -= st2->dropped;
}
void add_stats(struct stats* st1, struct stats* st2){
  st1->total_bytes += st2->total_bytes;
  st1->incomplete += st2->incomplete;
  st1->total_written += st2->total_written;
  st1->dropped += st2->dropped;
}
void print_intermediate_stats(struct stats *stats){
  LOG("Net Send/Receive completed: \t%luMb/s\n"
      "HD Read/write completed \t%luMb/s\n"
      "Dropped %lu\tIncomplete %lu\n"
      ,BYTES_TO_MBITSPS(stats->total_bytes), BYTES_TO_MBITSPS(stats->total_written), stats->dropped, stats->incomplete);
}
void print_stats(struct stats *stats, struct opt_s * opts){
  if(opts->optbits & READMODE){
    LOG("Stats for %s \n"
	"Packets: %lu\n"
	"Bytes: %lu\n"
	"Read: %lu\n"
	"Time: %lus\n"
	"Files: %lu\n"
	"HD-failures: %d\n"
	//"Net send Speed: %fMb/s\n"
	//"HD read Speed: %fMb/s\n"
	,opts->filename, stats->total_packets, stats->total_bytes, stats->total_written,opts->time, opts->cumul,opts->hd_failures);//, (((float)stats->total_bytes)*(float)8)/((float)1024*(float)1024*opts->time), (stats->total_written*8)/(1024*1024*opts->time));
  }
  else{
    LOG("Stats for %s \n"
	"Packets: %lu\n"
	"Bytes: %lu\n"
	"Dropped: %lu\n"
	"Incomplete: %lu\n"
	"Written: %lu\n"
	"Time: %lu\n"
	"Files: %lu\n"
	"HD-failures: %d\n"
	"Net receive Speed: %luMb/s\n"
	"HD write Speed: %luMb/s\n"
	,opts->filename, stats->total_packets, stats->total_bytes, stats->dropped, stats->incomplete, stats->total_written,opts->time, opts->cumul,opts->hd_failures, (stats->total_bytes*8)/(1024*1024*opts->time), (stats->total_written*8)/(1024*1024*opts->time));
  }
}
int clear_and_default(struct opt_s* opt){
  memset(opt, 0, sizeof(struct opt_s));
  opt->filename = NULL;
  opt->device_name = NULL;
  opt->cfgfile = NULL;

  opt->diskids = 0;
  opt->hd_failures = 0;
#if(DAEMON)
  opt->status = STATUS_NOT_STARTED;
#endif

  config_init(&(opt->cfg));
  /* Opts using optbits */
  //opt->capture_type = CAPTURE_W_FANOUT;
  opt->optbits |= CAPTURE_W_UDPSTREAM;
  opt->do_w_stuff_every = HD_MIN_WRITE_SIZE;
  //opt->fanout_type = PACKET_FANOUT_LB;
  //opt->optbits |= PACKET_FANOUT_LB;
  opt->root_pid = getpid();
  opt->port = 2222;
  opt->n_threads = 0;
  opt->n_drives = 1;
  opt->packet_size = DEF_BUF_ELEM_SIZE;
  opt->cumul_found = 0;

  //opt->optbits |=USE_RX_RING;
  //TODO: Add option for choosing backend
  //opt->buf_type = BUFFER_RINGBUF;
  opt->optbits |= BUFFER_SIMPLE;
  /* Calculated automatically when aligment is calculated */
  //opt->filesize = FILE_SPLIT_TO_BLOCKS;
  //opt->rec_type= REC_DEF;
  opt->optbits |= REC_DEF;
  opt->taken_rpoints = 0;
  opt->rate = 10000;
  opt->minmem = MIN_MEM_GIG;
  opt->maxmem = MAX_MEM_GIG;
  //opt->handle = 0;
  //opt->read = 0;
  opt->tid = 0;
  //opt->async = 0;
  //opt->optbits = 0xff000000;
  opt->optbits |= SIMPLE_BUFFER;
  opt->socket = 0;
  return 0;
}
int parse_options(int argc, char **argv, struct opt_s* opt){
  int ret,i;

  while((ret = getopt(argc, argv, "d:i:t:s:n:m:w:p:qur:a:vVI:A:W:xc:"))!= -1){
    switch (ret){
      case 'i':
	opt->device_name = strdup(optarg);
	break;
      case 'c':
	opt->cfgfile = (char*)malloc(sizeof(char)*FILENAME_MAX);
	CHECK_ERR_NONNULL(opt->cfgfile, "Cfgfile malloc");
	opt->cfgfile = strdup(optarg);
	break;
      case 'v':
	opt->optbits |= VERBOSE;
	break;
      case 'd':
	opt->n_drives = atoi(optarg);
	break;
      case 'I':
	opt->minmem = atoi(optarg);
	break;
      case 'x':
	opt->optbits |= USE_RX_RING;
	break;
      case 'W':
	opt->do_w_stuff_every = atoi(optarg)*MEG;
	break;
      case 'A':
	opt->maxmem = atoi(optarg);
	break;
      case 'V':
	opt->optbits |= MOUNTPOINT_VERBOSE;
	break;
      case 't':
	opt->optbits &= ~LOCKER_CAPTURE;
	if (!strcmp(optarg, "fanout")){
	  //opt->capture_type = CAPTURE_W_FANOUT;
	  opt->optbits |= CAPTURE_W_FANOUT;
	}
	else if (!strcmp(optarg, "udpstream")){
	  //opt->capture_type = CAPTURE_W_UDPSTREAM;
	  opt->optbits |= CAPTURE_W_UDPSTREAM;
	}
	else if (!strcmp(optarg, "sendfile")){
	  //opt->capture_type = CAPTURE_W_SPLICER;
	  opt->optbits |= CAPTURE_W_SPLICER;
	}
	else {
	  LOGERR("Unknown packet capture type [%s]\n", optarg);
	  usage(argv[0]);
	  exit(1);
	}
	break;
	/* Fanout choosing removed and set to default LB since
	 * Implementation not that feasible anyway
	 case 'a':
	 opt->optbits &= ~LOCKER_FANOUT;
	 if (!strcmp(optarg, "hash")){
	//opt->fanout_type = PACKET_FANOUT_HASH;
	opt->optbits |= PACKET_FANOUT_HASH;
	}
	else if (!strcmp(optarg, "lb")){
	//opt->fanout_type = PACKET_FANOUT_LB;
	opt->optbits |= PACKET_FANOUT_LB;
	}
	else {
	LOGERR("Unknown fanout type [%s]\n", optarg);
	usage(argv[0]);
	exit(1);
	}
	break;
	*/
      case 'a':
#ifdef HAVE_RATELIMITER
	opt->optbits |= WAIT_BETWEEN;
	opt->wait_nanoseconds = atoi(optarg)*1000;
	ZEROTIME(opt->wait_last_sent);
#else
	LOGERR("STREAMER: Rate limiter not compiled\n");
#endif
	break;
      case 'r':
	opt->rate = atoi(optarg);
	break;
      case 's':
	opt->port = atoi(optarg);
	break;
      case 'p':
	opt->packet_size = atoi(optarg);
	break;
      case 'u':
#ifdef HAVE_HUGEPAGES
	opt->optbits |= USE_HUGEPAGE;
#endif
	break;
      case 'n':
	opt->n_threads = atoi(optarg);
	break;
      case 'q':
#ifdef CHECK_OUT_OF_ORDER
	//opt->handle |= CHECK_SEQUENCE;
	opt->optbits |= CHECK_SEQUENCE;
	break;
#endif
      case 'm':
	if (!strcmp(optarg, "r")){
	  opt->optbits &= ~READMODE;
	  //opt->read = 0;
	}
	else if (!strcmp(optarg, "s")){
	  //opt->read = 1;
	  opt->optbits |= READMODE;
	}
	else {
	  LOGERR("Unknown mode type [%s]\n", optarg);
	  usage(argv[0]);
	  exit(1);
	}
	break;
      case 'w':
	opt->optbits &= ~LOCKER_REC;
	if (!strcmp(optarg, "def")){
	  /*
	     opt->rec_type = REC_DEF;
	     opt->async = 0;
	     */
	  opt->optbits |= REC_DEF;
	  opt->optbits &= ~ASYNC_WRITE;
	}
#ifdef HAVE_LIBAIO
	else if (!strcmp(optarg, "aio")){
	  /*
	     opt->rec_type = REC_AIO;
	     opt->async = 1;
	     */
	  opt->optbits |= REC_AIO|ASYNC_WRITE;
	}
#endif
	else if (!strcmp(optarg, "splice")){
	  /*
	     opt->rec_type = REC_SPLICER;
	     opt->async = 0;
	     */
	  opt->optbits |= REC_SPLICER;
	  opt->optbits &= ~ASYNC_WRITE;
	}
	else if (!strcmp(optarg, "dummy")){
	  /*
	     opt->rec_type = REC_DUMMY;
	     opt->buf_type = WRITER_DUMMY;
	     */
	  opt->optbits &= ~LOCKER_WRITER;
	  opt->optbits |= REC_DUMMY|WRITER_DUMMY;
	  opt->optbits &= ~ASYNC_WRITE;
	}
	else {
	  LOGERR("Unknown mode type [%s]\n", optarg);
	  usage(argv[0]);
	  exit(1);
	}
	break;
      default:
	usage(argv[0]);
	exit(1);
    }
  }
#if(!DAEMON)
  if(argc -optind != 2){
    usage(argv[0]);
    exit(1);
  }
#endif
  argv +=optind;
  argc -=optind;

  if(opt->cfgfile!=NULL){
    LOG("Path for cfgfile specified. All command line options specced in this file will be ignored\n");
    ret = read_full_cfg(opt);
    if(ret != 0){
      E("Error parsing cfg file. Exiting");
      return -1;
    }
  }

  /* If we're using rx-ring, then set the packet size to +TPACKET_HDRLEN */
  /*
     if(opt->optbits & USE_RX_RING)
     opt->packet_size += TPACKET_HDRLEN;
     */

  /* If n_threads isn't set, set it to n_drives +2 	*/
  /* Not used. Maxmem limits this instead		*/
  /*
  if(opt->n_threads == 0)
    opt->n_threads = opt->n_drives +2;
    */

#if(!DAEMON)
  opt->filename = (char*)malloc(sizeof(char)*FILENAME_MAX);
  CHECK_ERR_NONNULL(opt->filename, "filename malloc");
  if(strcpy(opt->filename, argv[0]) == NULL){
    E("strcpy filename");
    return -1;
  }
  CHECK_ERR_NONNULL(err);
  //opt->filename = argv[0];
#endif
  //opt->points = (struct rec_point *)calloc(opt->n_drives, sizeof(struct rec_point));
  for(i=0;i<opt->n_drives;i++){
    opt->filenames[i] = malloc(sizeof(char)*FILENAME_MAX);
    CHECK_ERR_NONNULL(opt->filenames[i], "filename malloc");
    //opt->filenames[i] = (char*)malloc(FILENAME_MAX);
    sprintf(opt->filenames[i], "%s%d%s%s%s", ROOTDIRS, i, "/", opt->filename,"/");
  }
#if(!DAEMON)
  if(opt->optbits & READMODE){
    opt->hostname = (char*)malloc(sizeof(char)*IP_LENGTH);
    if(strcpy(opt->hostname, argv[1]) == NULL){
      E("strcpy hostname");
      return -1;
    }
    //opt->hostname = argv[1];
  }
  else
    opt->time = atoi(argv[1]);
#endif
  opt->cumul = 0;

  struct rlimit rl;
  /* Query max size */
  /* TODO: Doesn't work properly althought mem seems to be unlimited */
  ret = getrlimit(RLIMIT_DATA, &rl);
  if(ret < 0){
    LOGERR("Failed to get rlimit of memory\n");
    exit(1);
  }
#if(DEBUG_OUTPUT)
  LOG("STREAMER: Queried max mem size %ld \n", rl.rlim_cur);
#endif
  /* Check for memory limit						*/
  //unsigned long minmem = MIN_MEM_GIG*GIG;
  if (opt->minmem > rl.rlim_cur && rl.rlim_cur != RLIM_INFINITY){
#if(DEBUG_OUTPUT)
    LOG("STREAMER: Limiting memory to %lu\n", rl.rlim_cur);
#endif
    opt->minmem = rl.rlim_cur;
  }
  //if(!(opt->optbits & READMODE)){
  /*
  if (CALC_BUF_SIZE(opt) != 0)
    return -1;
    */
  //}
  return 0;
}
int init_branches(struct opt_s *opt){
  int err;
  opt->membranch = (struct entity_list_branch*)malloc(sizeof(struct entity_list_branch));
  CHECK_ERR_NONNULL(opt->membranch, "membranch malloc");
  opt->diskbranch = (struct entity_list_branch*)malloc(sizeof(struct entity_list_branch));
  CHECK_ERR_NONNULL(opt->diskbranch, "diskbranch malloc");

  opt->membranch->freelist = NULL;
  opt->membranch->busylist = NULL;
  opt->membranch->loadedlist = NULL;
  opt->diskbranch->freelist = NULL;
  opt->diskbranch->busylist = NULL;
  opt->diskbranch->loadedlist = NULL;

  err = pthread_mutex_init(&(opt->membranch->branchlock), NULL);
  CHECK_ERR("branchlock");
  err = pthread_mutex_init(&(opt->diskbranch->branchlock), NULL);
  CHECK_ERR("branchlock");
  err = pthread_cond_init(&(opt->membranch->busysignal), NULL);
  CHECK_ERR("busysignal");
  err = pthread_cond_init(&(opt->diskbranch->busysignal), NULL);
  CHECK_ERR("busysignal");
  return 0;
}
int init_rbufs(struct opt_s *opt){
  int i, err;
  err = CALC_BUF_SIZE(opt);
  CHECK_ERR("calc bufsize");
  D("Here we are with nthreads as %d",, opt->n_threads);
#ifdef PRIORITY_SETTINGS
  memset(&opt->param, 0, sizeof(param));
  err = pthread_attr_init(&opt->pta);
  if(err != 0){
    E("Pthread attr initialization: %s",,strerror(err));
    return -1;
  }
#endif
#ifdef TUNE_AFFINITY
  long processors = sysconf(_SC_NPROCESSORS_ONLN);
  D("Polled %ld processors",,processors);
  int cpusetter =2;
  CPU_ZERO(&opt->cpuset);
#endif

  /*
  if(opt->optbits & READMODE){
  }
  */
  opt->rbuf_pthreads = (pthread_t*)malloc(sizeof(pthread_t)*opt->n_threads);

  opt->bes = (struct buffer_entity*)malloc(sizeof(struct buffer_entity)*opt->n_threads);
  CHECK_ERR_NONNULL(opt->bes, "buffer entity malloc");

#ifdef PRIORITY_SETTINGS
  err = pthread_attr_getschedparam(&(opt->pta), &(opt->param));
  if(err != 0)
    E("Error getting schedparam for pthread attr: %s",,strerror(err));
  else
    D("Schedparam set to %d, Trying to set to minimun %d",, opt->param.sched_priority, MIN_PRIO_FOR_PTHREAD);

  err = pthread_attr_setschedpolicy(&(opt->pta), SCHED_FIFO);
  if(err != 1)
    E("Error setting schedtype for pthread attr: %s",,strerror(err));

  opt->param.sched_priority = MIN_PRIO_FOR_PTHREAD;
  err = pthread_attr_setschedparam(&(opt->pta), &(opt->param));
  if(rc != 0)
    E("Error setting schedparam for pthread attr: %s",,strerror(err));
#endif

#if(DEBUG_OUTPUT)
  LOG("STREAMER: Initializing threads\n");
#endif

  for(i=0;i<opt->n_threads;i++){
    //int err = 0;
    //be = (struct buffer_entity*)malloc(sizeof(struct buffer_entity));
    //CHECK_ERR_NONNULL(be, "be malloc");

    err = sbuf_init_buf_entity(opt, &(opt->bes[i]));
    if(err != 0){
      LOGERR("Error in buffer init\n");
      exit(-1);
    }
    //TODO: Change write loop to just loop. Now means both read and write
    D("Starting buffer thread");
#ifdef PRIORITY_SETTINGS
    err = pthread_create(&(opt->rbuf_pthreads[i]), &(opt->pta), (opt->bes[i]).write_loop,(void*)&(opt->bes[i]));
#else
    err = pthread_create(&(opt->rbuf_pthreads[i]), NULL, opt->bes[i].write_loop,(void*)&(opt->bes[i]));
#endif
    CHECK_ERR("pthread create");
#ifdef TUNE_AFFINITY
    if(cpusetter == processors)
      cpusetter = 1;
    CPU_SET(cpusetter,&(opt->cpuset));
    cpusetter++;

    D("Tuning buffer thread %i to processor %i",,i,cpusetter);
    err = pthread_setaffinity_np(opt->rbuf_pthreads[i], sizeof(cpu_set_t), &(opt->cpuset));
    if(err != 0){
      perror("Affinity");
      E("Error: setting affinity");
    }
    CPU_ZERO(&(opt->cpuset));
#endif //TUNE_AFFINITY
    D("Pthread number %d got id %lu",, i,opt->rbuf_pthreads[i]);
  }
  //pthread_t rbuf_pthreads[opt->n_threads];
  //long unsigned * y_u_touch_this = &rbuf_pthreads[27];
  return 0;
}
int close_rbufs(struct opt_s *opt, struct stats* da_stats){
  int i,err;
  // Stop the memory threads 
  oper_to_all(opt->membranch, BRANCHOP_STOPANDSIGNAL, NULL);
#ifndef UGLY_FIX_ON_RBUFTHREAD_EXIT
  for(i =0 ;i<opt->n_threads;i++){
    err = pthread_join(opt->rbuf_pthreads[i], NULL);
    if (err<0) {
      printf("ERROR; return code from pthread_join() is %d\n", err);
    }
    else
      D("%dth buffer exit OK",,i);
  }
#endif //UGLY_FIX_ON_RBUFTHREAD_EXIT
  free(opt->rbuf_pthreads);
  D("Getting stats and closing for membranch");
  oper_to_all(opt->membranch, BRANCHOP_CLOSERBUF, (void*)da_stats);

  free(opt->membranch);
  free(opt->bes);

  return 0;
}
int close_opts(struct opt_s *opt){
  if(opt->device_name != NULL)
    free(opt->device_name);
  if(opt->optbits & READMODE){
    if(opt->fileholders != NULL)
      free(opt->fileholders);
  }
  if(opt->cfgfile != NULL){
    free(opt->cfgfile);
  }
  if(opt->hostname != NULL)
    free(opt->hostname);
  if(opt->filename != NULL)
    free(opt->filename);
  config_destroy(&(opt->cfg));
#ifdef PRIORITY_SETTINGS
  pthread_attr_destroy(&(opt->pta));
#endif
  free(opt);
  return 0;
}
int init_recp(struct opt_s *opt){
  int err, i;
  opt->recs = (struct recording_entity*)malloc(sizeof(struct recording_entity)*opt->n_drives);
  CHECK_ERR_NONNULL(opt->recs, "rec entity malloc");
  for(i=0;i<opt->n_drives;i++){
    //struct recording_entity * re = (struct recording_entity*)malloc(sizeof(struct recording_entity));
    /*
       struct listed_entity *le = (struct listed_entity*)malloc(sizeof(struct listed_entity));
       le->entity = (void*)re;
       add_to_entlist(opt.diskbranch, le);
       */

    /*
     * NOTE: AIOW-stuff and udp-streamer are bidirectional and
     * only require the setting of opt->read to one for 
     * sending stuff
     */
    switch(opt->optbits & LOCKER_REC){
#if HAVE_LIBAIO
      case REC_AIO:
	err = aiow_init_rec_entity(opt, &(opt->recs[i]));
	//NOTE: elem_size is read inside if we're reading
	break;
#endif
      case REC_DUMMY:
	err = common_init_dummy(opt, &(opt->recs[i]));
	break;
      case REC_DEF:
	err = def_init_def(opt, &(opt->recs[i]));
	break;
      case REC_SPLICER:
	err = splice_init_splice(opt, &(opt->recs[i]));
	break;
    }
    if(err != 0){
      LOGERR("Error in writer init\n");
      /* TODO: Need to free all kinds of stuff if init goes bad */
      /* in the writer itself 					*/
      //free(re);
      //exit(-1);
    }
    /* Add the recording entity to the diskbranch */
  }
  return 0;
}
int close_recp(struct opt_s *opt, struct stats* da_stats){
  int i;
  oper_to_all(opt->diskbranch, BRANCHOP_CLOSEWRITER, (void*)da_stats);
  for(i=0;i<opt->n_drives;i++){
    free(opt->filenames[i]);
  }
  free(opt->diskbranch);
  free(opt->recs);
  return 0;
}
#if(DAEMON)
void* vlbistreamer(void *opti)
#else
int main(int argc, char **argv)
#endif
{
  int err = 0;
#ifdef HAVE_LRT
  struct timespec start_t;
#endif

#if(DAEMON)
  struct opt_s *opt = (struct opt_s*)opti;
#else
  struct opt_s *opt = malloc(sizeof(struct opt_s));
  CHECK_ERR_NONNULL(opt, "opt malloc");
#if(DEBUG_OUTPUT)
  LOG("STREAMER: Reading parameters\n");
#endif
  clear_and_default(opt);
  err = parse_options(argc,argv,opt);
  if(err != 0)
    exit(-1);
#endif //DAEMON

  /*
     switch(opt.capture_type){
     case CAPTURE_W_FANOUT:
     n_threads = THREADS;
     break;
     case CAPTURE_W_UDPSTREAM:
     n_threads = UDP_STREAM_THREADS;
     break;
     }
     */
  //struct streamer_entity threads[opt.n_threads];
  struct streamer_entity streamer_ent;

  pthread_t streamer_pthread;
  struct stats* stats_full = (struct stats*)malloc(sizeof(struct stats));


  //pthread_attr_t attr;

  /* Handle hostname etc */
  /* TODO: Whats the best way that accepts any format? */
  if(opt->optbits & READMODE){
    struct hostent *hostptr;

    hostptr = gethostbyname(opt->hostname);
    if(hostptr == NULL){
      perror("Hostname");
      exit(-1);
    }
    memcpy(&(opt->serverip), (char *)hostptr->h_addr, sizeof(opt->serverip));

#if(DEBUG_OUTPUT)
    LOG("STREAMER: Resolved hostname\n");
#endif
  }

#if(!DAEMON)
  err = init_branches(opt);
  CHECK_ERR("init branches");
  err = init_recp(opt);
  CHECK_ERR("init recpoints");
#endif

#ifdef HAVE_LIBCONFIG_H
  err = init_cfg(opt);
  //TODO: cfg destruction
  if(err != 0){
    E("Error in cfg init");
    FREE_AND_ERROREXIT
  }
#endif

#if(!DAEMON)
  err = init_rbufs(opt);
  CHECK_ERR("init rbufs");
#endif //DAEMON
  /* Check and set cfgs at this point */
  //return -1;

  /* If we're sending stuff, check all the diskbranch members for the files they have 	*/
  /* Also updates the fileholders list to point the owners of files to correct drives	*/
#ifdef HAVE_LIBCONFIG_H
  if(opt->optbits &READMODE){
    oper_to_all(opt->diskbranch,BRANCHOP_CHECK_FILES,(void*)opt);
    LOG("For recording %s: %lu files were found out of %lu total.\n", opt->filename, opt->cumul_found, opt->cumul);
  }
#endif
  /* Now we have all the object data, so we can calc our buffer sizes	*/
  /* If we're using the rx-ring, reserve space for it here */
  /*
     if(opt.optbits & USE_RX_RING){
     int flags = MAP_ANONYMOUS|MAP_SHARED;
     if(opt.optbits & USE_HUGEPAGE)
     flags |= MAP_HUGETLB;
     opt.buffer = mmap(NULL, ((unsigned long)sbuf->opt->buf_num_elems)*((unsigned long)sbuf->opt->packet_size)*opt.n_threads, PROT_READ|PROT_WRITE , flags, 0,0);
     }
     */



  /* Format the capturing thread */
  switch(opt->optbits & LOCKER_CAPTURE)
  {
    case CAPTURE_W_UDPSTREAM:
      if(opt->optbits & READMODE)
	err = udps_init_udp_sender(opt, &(streamer_ent));
      else
	err = udps_init_udp_receiver(opt, &(streamer_ent));
      break;
    case CAPTURE_W_FANOUT:
      err = fanout_init_fanout(opt, &(streamer_ent));
      break;
    case CAPTURE_W_SPLICER:
      //err = sendfile_init_writer(&opt, &(streamer_ent));
      break;
    default:
      LOG("DUR %X\n", opt->optbits);
      break;

  }
  if(err != 0){
    LOGERR("Error in thread init\n");
    exit(-1);
  }

  printf("STREAMER: In main, starting receiver thread \n");

#ifdef PRIORITY_SETTINGS
  param.sched_priority = MAX_PRIO_FOR_PTHREAD;
  err = pthread_attr_setschedparam(&(opt->pta), &(opt->param));
  if(err != 0)
    E("Error setting schedparam for pthread attr: %s, to %d",, strerror(err), MAX_PRIO_FOR_PTHREAD);
  err = pthread_create(&streamer_pthread, &(opt->pta), streamer_ent.start, (void*)&streamer_ent);
#else
  err = pthread_create(&streamer_pthread, NULL, streamer_ent.start, (void*)&streamer_ent);
#endif
  if (err != 0){
    printf("ERROR; return code from pthread_create() is %d\n", err);
    STREAMER_ERROR_EXIT;
  }
#ifdef TUNE_AFFINITY
  /* Put the capture on the first core */
  CPU_SET(0,&(opt->cpuset));
  /*
     cpusetter++;
     if(cpusetter > processors)
     cpusetter = 1;
     */

  err = pthread_setaffinity_np(streamer_pthread, sizeof(cpu_set_t), &cpuset);
  if(err != 0){
    E("Error: setting affinity: %d",,err);
  }
  CPU_ZERO(&cpuset);
#endif
  //Spread processes out to n cores
  //NOTE: setaffinity should be used after thread has been started

  //}
#if(DAEMON)
  //opt->status = STATUS_RUNNING;
#endif

  init_stats(stats_full);
  /* HERP so many ifs .. WTB Refactoring time*/
  if(opt->optbits & READMODE){
#ifdef HAVE_LRT
    clock_gettime(CLOCK_REALTIME, &start_t);
#else
    //TODO
#endif
  }
  /* If we're capturing, time the threads and run them down after we're done */
  /* Print speed etc. */
  if(opt->optbits & VERBOSE){

    /* Init the stats */
    struct stats *stats_prev, *stats_now;//, stats_temp;
    stats_prev = (struct stats*)malloc(sizeof(struct stats));
    STREAMER_CHECK_NONNULL(stats_prev, "stats malloc");
    stats_now = (struct stats*)malloc(sizeof(struct stats));
    STREAMER_CHECK_NONNULL(stats_now, "stats malloc");
    //memset(stats_prev, 0,sizeof(struct stats));
    //memset(stats_now, 0,sizeof(struct stats));
    /* MEmset is doing weird stuff 	*/
    init_stats(stats_prev);
    init_stats(stats_now);
    int sleeptodo;
    //memset(&stats_now, 0,sizeof(struct stats));
    LOG("STREAMER: Printing stats per second\n");
    LOG("----------------------------------------\n");

    if(opt->optbits & READMODE)
      sleeptodo= 1;
    else
      sleeptodo = opt->time;
    while(sleeptodo >0 && streamer_ent.is_running(&streamer_ent)){
      sleep(1);
      //memset(stats_now, 0,sizeof(struct stats));
      init_stats(stats_now);

      streamer_ent.get_stats(streamer_ent.opt, stats_now);
      /* Query and print the stats */
      /*
	 for(i=0;i<opt.n_threads;i++){
      //threads[i].get_stats(threads[i].opt, &stats_now);
      if(threads[i].be->recer->get_stats != NULL)
      threads[i].be->recer->get_stats(threads[i].be->recer->opt, &stats_now);
      }
      */
      //TODO: Write end stats
      oper_to_all(opt->diskbranch,BRANCHOP_GETSTATS,(void*)stats_now);

      //memcpy(&stats_temp, &stats_now, sizeof(struct stats));
      neg_stats(stats_now, stats_prev);

      print_intermediate_stats(stats_now);
      //LOG("Time %ds \t------------------------\n", opt.time-sleeptodo+1);
      if(!(opt->optbits & READMODE)){
	LOG("Time %lds\n", opt->time-sleeptodo+1);
	//LOG("Files: %ld\n", stats_now.files_exchanged);
      }
      else{
	LOG("Time %ds\n", sleeptodo);
	//LOG("Files: %ld/%ld\n", stats_now.files_exchanged, opt->cumul_found);
      }

      LOG("Ringbuffers: ");
      print_br_stats(opt->membranch);
      LOG("Recpoints: ");
      print_br_stats(opt->diskbranch);

      LOG("----------------------------------------\n");

      if(!(opt->optbits & READMODE))
	sleeptodo--;
      else
	sleeptodo++;
      add_stats(stats_prev, stats_now);
      //memcpy(&stats_prev, &stats_temp, sizeof(struct stats));
      /*
	 if(opt.optbits & READMODE){
	 if(opt.cumul >= opt.max_num_packets-1)
	 sleeptodo = 0;
	 }
	 */
    }
    free(stats_now);
    free(stats_prev);
  }
  else{
    if(!(opt->optbits & READMODE)){
      sleep(opt->time);
      ////pthread_mutex_destroy(opt.cumlock);
    }
  }
  /* Close the sockets on readmode */
  if(!(opt->optbits & READMODE)){
    //for(i = 0;i<opt.n_threads;i++){
    //threads[i].stop(&(threads[i]));
    //}
    streamer_ent.stop(&(streamer_ent));
    udps_close_socket(&streamer_ent);
    //threads[0].close_socket(&(threads[0]));
    //streamer_ent.close_socket(&(streamer_ent));
  }
  //for (i = 0; i < opt.n_threads; i++) {
  err = pthread_join(streamer_pthread, NULL);
  if (err<0) {
    printf("ERROR; return code from pthread_join() is %d\n", err);
  }
  else
    D("Streamer thread exit OK");
#if(DEBUG_OUTPUT)
  LOG("STREAMER: Threads finished. Getting stats\n");
#endif

  if(opt->optbits & READMODE){
    /* Too fast sending so I'll keep this in ticks and use floats in stats */
#ifdef HAVE_LRT
    struct timespec end_t;
    clock_gettime(CLOCK_REALTIME, &end_t);
    opt->time = ((end_t.tv_sec * BILLION + end_t.tv_nsec) - (start_t.tv_sec*BILLION + start_t.tv_nsec))/BILLION;
    //LOG("END: %lus %luns, START: %lus, %luns\n", end_t.tv_sec, end_t.tv_nsec, start_t.tv_sec, start_t.tv_nsec);
#else
    LOGERR("STREAMER: lrt not present. Setting time to 1\n");
    opt->time = 1;
    //opt.time = (clock() - start_t);
#endif
  }

  streamer_ent.close(streamer_ent.opt, (void*)stats_full);
#if(!DAEMON)
  close_rbufs(opt, stats_full);
  close_recp(opt,stats_full);
  D("Membranch and diskbranch shut down");
#endif

#if(DAEMON)
  opt->status = STATUS_FINISHED;
#endif

  D("Printing stats");
  print_stats(stats_full, opt);
  free(stats_full);
  D("Stats over");

#if(DAEMON)
  close_opts(opt);
  pthread_exit(NULL);
#else
  STREAMER_EXIT;
#endif
}
/* These two separated here */
int write_cfg(config_t *cfg, char* filename){
  int err = config_write_file(cfg,filename);
  if(err == CONFIG_FALSE){
    E("Failed to write CFG to %s",,filename);
    return -1;
  }
  else
    return 0;
}
int read_cfg(config_t *cfg, char * filename){
  int err = config_read_file(cfg,filename);
  if(err == CONFIG_FALSE){
    E("%s:%d - %s",, filename, config_error_line(cfg), config_error_text(cfg));
    E("Failed to read CFG from %s",,filename);
    return -1;
  }
  else
    return 0;
}
