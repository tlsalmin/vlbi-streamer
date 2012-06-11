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
#define TUNE_AFFINITY
#define PRIORITY_SETTINGS
#define KB 1024
#define BYTES_TO_MBITSPS(x)	(x*8)/(KB*KB)
//#define UGLY_HACKS_ON_STATS
//TODO: Search these
#define MAX_PRIO_FOR_PTHREAD 4
#define MIN_PRIO_FOR_PTHREAD 1

#define BRANCHOP_STOPANDSIGNAL 1
#define BRANCHOP_GETSTATS 2
#define BRANCHOP_CLOSERBUF 3
#define BRANCHOP_CLOSEWRITER 4
#define BRANCHOP_WRITE_CFGS 5
#define BRANCHOP_READ_CFGS 6
#define BRANCHOP_CHECK_FILES 7

#define FREE_AND_ERROREXIT if(opt->device_name != NULL){free(opt->device_name);} if(opt->optbits & READMODE){ if(opt->fileholders != NULL) free(opt->fileholders); } config_destroy(&(opt->cfg)); free(opt->membranch); free(opt->diskbranch); pthread_attr_destroy(&pta);exit(-1);

/* This should be more configurable */
extern char *optarg;
extern int optind, optopt;

//static struct opt_s opt;

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
struct listed_entity* get_w_check(struct listed_entity **lep, int seq, struct listed_entity **other, struct listed_entity **other2, struct entity_list_branch* br){
  struct listed_entity *temp = *lep;
  struct listed_entity *le = NULL;
  while(le== NULL){
    while(temp != NULL){
      if(temp->check(temp->entity, seq) == 1){
	le = temp;
	break;
      }
      else{
	temp = temp->child;
      }
    }
    /* If le wasn't found in the list */
    if(le == NULL){
      /* Check if branch is dead */
      if(*lep == NULL && *other == NULL && *other2 == NULL){
	E("No entities in list. Returning NULL");
	return NULL;
      }
      D("Failed to get free buffer. Sleeping waiting for %d",,seq);
      pthread_cond_wait(&(br->busysignal), &(br->branchlock));
      D("Woke up! Checking for %d again",, seq);
      temp = *lep;
    }
  }
  D("Found specific elem id %d!",, seq);
  return le;
}
/* Get a loaded buffer with the specific seq */
void* get_loaded(struct entity_list_branch *br, unsigned long seq){
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
void* get_specific(struct entity_list_branch *br,void * opt,unsigned long seq, unsigned long bufnum, unsigned long id, int* acquire_result)
{
  pthread_mutex_lock(&(br->branchlock));
  struct listed_entity* temp = get_w_check(&br->freelist, id, &br->busylist, &br->loadedlist, br);
  
  if(temp ==NULL){
    pthread_mutex_unlock(&(br->branchlock));
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
void* get_free(struct entity_list_branch *br,void * opt,unsigned long seq, unsigned long bufnum, int* acquire_result)
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
void set_busy(struct entity_list_branch *br, struct listed_entity* en)
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
      /* once before we try to check the option due to needing buf_elem_size	*/
      else if(check == 1){
	if((unsigned long)config_setting_get_int64(setting) != opt->buf_elem_size*opt->buf_num_elems)
	  return -1;
      }
      else if(write == 1){
	filesize = opt->buf_elem_size*opt->buf_num_elems;
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
    CFG_FULL_UINT64(opt->buf_elem_size, "buf_elem_size")
    CFG_FULL_INT(opt->buf_num_elems, "buf_num_elems")
    CFG_FULL_INT(opt->buf_division, "buf_division")
    CFG_FULL_STR(hostname)
    CFG_FULL_UINT64(opt->serverip, "serverip")
    CFG_FULL_UINT64(opt->total_packets, "total_packets")

    setting = config_setting_get_elem(root,++index);
  }
  /* Only use filesize here, since it needs buf_elem_size */
  if(filesize_found==1){
    opt->buf_num_elems = filesize/opt->buf_elem_size;
    opt->do_w_stuff_every = filesize/((unsigned long)opt->buf_division);
  }
  
  return 0;
}
int stub_rec_cfg(config_setting_t *root){
  config_setting_t *setting;
  setting = config_setting_add(root, "buf_elem_size", CONFIG_TYPE_INT64);
  CHECK_ERR_NONNULL(setting, "add buf_elem_size");
  //config_setting_set_int64(setting, opt->buf_elem_size);
  setting = config_setting_add(root, "cumul", CONFIG_TYPE_INT64);
  CHECK_ERR_NONNULL(setting, "add cumul");
  setting = config_setting_add(root, "filesize", CONFIG_TYPE_INT64);
  CHECK_ERR_NONNULL(setting, "add filesize");
  setting = config_setting_add(root, "total_packets", CONFIG_TYPE_INT64);
  CHECK_ERR_NONNULL(setting, "add total_packets");
  setting = config_setting_add(root, "buf_division", CONFIG_TYPE_INT);
  CHECK_ERR_NONNULL(setting, "add buf_division");
  return 0;
}
int stub_full_cfg(config_setting_t *root){
  config_setting_t *setting;
  //stub_rec_cfg(root);
  CFG_ADD_INT64(buf_elem_size);
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
    //long long buf_elem_size=0,cumul=0,old_buf_elem_size=0,old_cumul=0;//,n_files=0,old_n_files=0;
    char * path = (char*) malloc(sizeof(char)*FILENAME_MAX);
    for(i=0;i<opt->n_drives;i++){
      sprintf(path, "%s%s%s", opt->filenames[i],opt->filename ,".cfg");
      if(! config_read_file(&(opt->cfg),path)){
	E("%s:%d - %s",, path, config_error_line(&opt->cfg), config_error_text(&opt->cfg));
      }
      else{
	LOG("Config found on %s\n",path); 
	root = config_root_setting(&(opt->cfg));
	if(found == 0){
	  D("Getting opts from first config");
	  set_from_root(opt,root,0,0);
	  found = 1;
	  opt->fileholders = (int*)malloc(sizeof(int)*(opt->cumul));
	  memset(opt->fileholders, -1,sizeof(int)*(opt->cumul));
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
  //unsigned long bufsize;// = opt.buf_elem_size;
  int found = 0;

  int extra= 0;
  if(opt->optbits & USE_RX_RING){
    while((opt->buf_elem_size %16)!= 0){
      if(opt->optbits  &READMODE)
	E("Shouldn't need this in sending with RX-ring!");
      opt->buf_elem_size++;
      extra++;
    }
    D("While using RX-ring we need to reserve %d extra bytes per buffer element",, extra);
  }

  /* TODO: do_w_stuff gets warped  from MB to num of elems*/
  LOG("STREAMER: Calculating total buffer size between "
      "%lu GB to %luGB,"
      " size %lu packets, "
      "Doing maximum %luMB size writes\n"
      ,opt->minmem, opt->maxmem, opt->buf_elem_size, opt->do_w_stuff_every/MEG);
  /* Set do_w_stuff to minimum wanted */
  /* First set do_w_stuff to be packet aligned */
  unsigned long temp = opt->do_w_stuff_every/opt->buf_elem_size;
  LOG("%lu\n",temp);
  opt->do_w_stuff_every = temp*(opt->buf_elem_size);

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
	opt->buf_num_elems = (opt->do_w_stuff_every*opt->buf_division)/opt->buf_elem_size;
	//opt->do_w_stuff_every = opt->do_w_stuff_every/opt->buf_elem_size;
	break;
      }
      opt->do_w_stuff_every+=opt->buf_elem_size;
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
	,opt->minmem, opt->maxmem, opt->buf_elem_size*(opt->buf_num_elems), opt->do_w_stuff_every,opt->buf_division ,opt->buf_num_elems);
    //LOG("STREAMER: Didnt find alignment for %lu on %d threads, with w_every %lu\n", opt->buf_elem_size,opt->n_threads, (opt->buf_elem_size*(opt->buf_num_elems))/opt->buf_division);
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
    //opt.filesize = opt->buf_num_elems*(opt->buf_elem_size);

    LOG("STREAMER: Alignment found between "
	"%lu GB to %luGB"
	", Each buffer having %lu MB"
	", Writing in %lu MB size blocks"
	", Elements in buffer %d"
	", Total used memory: %luMB\n"
	,opt->minmem, opt->maxmem, (opt->buf_elem_size*(opt->buf_num_elems))/MEG, (opt->do_w_stuff_every)/MEG, opt->buf_num_elems, (opt->buf_num_elems*opt->buf_elem_size*opt->n_threads)/MEG);
    //LOG("STREAMER: Alignment found for %lu size packet with %d threads at %lu with ringbuf in %lu blocks. hd write size as %lu\n", opt->buf_elem_size,opt->n_threads ,opt->buf_num_elems*(opt->buf_elem_size),opt->buf_division, (opt->buf_num_elems*opt->buf_elem_size)/opt->buf_division);
    return 0;
  }
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
void init_stat(struct stats *stats){
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
	"HD-failures: %d\n"
	//"Net send Speed: %fMb/s\n"
	//"HD read Speed: %fMb/s\n"
	,opts->filename, stats->total_packets, stats->total_bytes, stats->total_written,opts->time, opts->hd_failures);//, (((float)stats->total_bytes)*(float)8)/((float)1024*(float)1024*opts->time), (stats->total_written*8)/(1024*1024*opts->time));
  }
  else{
    LOG("Stats for %s \n"
	"Packets: %lu\n"
	"Bytes: %lu\n"
	"Dropped: %lu\n"
	"Incomplete: %lu\n"
	"Written: %lu\n"
	"Time: %lu\n"
	"HD-failures: %d\n"
	"Net receive Speed: %luMb/s\n"
	"HD write Speed: %luMb/s\n"
	,opts->filename, stats->total_packets, stats->total_bytes, stats->dropped, stats->incomplete, stats->total_written,opts->time, opts->hd_failures, (stats->total_bytes*8)/(1024*1024*opts->time), (stats->total_written*8)/(1024*1024*opts->time));
  }
}
int clear_and_default(struct opt_s* opt){
  memset(opt, 0, sizeof(struct opt_s));
  opt->filename = NULL;
  opt->device_name = NULL;
  opt->cfgfile = NULL;//(char*)malloc(sizeof(char)*FILENAME_MAX);

  opt->diskids = 0;
  opt->hd_failures = 0;

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
  opt->buf_elem_size = DEF_BUF_ELEM_SIZE;
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
	opt->buf_elem_size = atoi(optarg);
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
     opt->buf_elem_size += TPACKET_HDRLEN;
     */

  /* If n_threads isn't set, set it to n_drives +2 */
  if(opt->n_threads == 0)
    opt->n_threads = opt->n_drives +2;

#if(!DAEMON)
  opt->filename = argv[0];
#endif
  //opt->points = (struct rec_point *)calloc(opt->n_drives, sizeof(struct rec_point));
  for(i=0;i<opt->n_drives;i++){
    opt->filenames[i] = malloc(sizeof(char)*FILENAME_MAX);
    //opt->filenames[i] = (char*)malloc(FILENAME_MAX);
    sprintf(opt->filenames[i], "%s%d%s%s%s", ROOTDIRS, i, "/", opt->filename,"/");
  }
#if(!DAEMON)
  if(opt->optbits & READMODE)
    opt->hostname = argv[1];
  else
    opt->time = atoi(argv[1]);
#endif
  opt->cumul = 0;


  /* Calc the max per thread amount of packets we can receive */
  /* TODO: Systems with non-uniform diskspeeds stop writing after too many packets */
  //if(!(opt->optbits & READMODE)){
  /* Ok so rate = Mb/s. (rate * 1024*1024)/8 = bytes_per_sec */
  /* bytes_per_sec * bytes = total_bytes */
  /* total_bytes / threads = bytes per thread */
  /* bytes_per_thread/opt->buf_elem_size = packets_per_thread */

  /* Making this very verbose, since its only done once */
  /*
     loff_t bytes_per_sec = (((unsigned long)opt->rate)*1024l*1024l)/8;
     loff_t bytes_per_thread_per_sec = bytes_per_sec/((unsigned long)opt->n_threads);
     loff_t bytes_per_thread = bytes_per_thread_per_sec*((unsigned long)opt->time);
     loff_t packets_per_thread = bytes_per_thread/((unsigned long)opt->buf_elem_size);
  //loff_t prealloc_bytes = (((unsigned long)opt->rate)*opt->time*1024)/(opt->buf_elem_size);
  */
  //Split kb/gb stuff to avoid overflow warning
  //prealloc_bytes = (prealloc_bytes*1024*8)/opt->n_threads;
  /* TODO this is quite bad as might confuse this for actual number of packets, not bytes */
  //opt->max_num_packets = packets_per_thread;
#if(DEBUG_OUTPUT)
  //LOG("Calculated with rate %d we would get %lu B/s a total of %lu bytes per thread and %lu packets per thread\n", opt->rate, bytes_per_sec, bytes_per_thread, packets_per_thread);
#endif
  //}

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
  if(!(opt->optbits & READMODE)){
    if (calculate_buffer_sizes(opt) != 0)
      exit(-1);
  }
  return 0;
}
#ifdef DAEMON
int vlbistreamer(struct opt_s *opt)
#else
int main(int argc, char **argv)
#endif
{
  int i,rc;
  int err = 0;
#ifndef DAEMON
  struct opt_s *opt = malloc(sizeof(struct opt_s));
#endif
#ifdef PRIORITY_SETTINGS
  pthread_attr_t        pta;
  struct sched_param    param;
  memset(&param, 0, sizeof(param));
  rc = pthread_attr_init(&pta);
  if(rc != 0){
    E("Pthread attr initialization: %s",,strerror(rc));
    return -1;
  }
#endif

#ifdef HAVE_LRT
  struct timespec start_t;
#endif

#ifdef HAVE_LIBCONFIG_H
#endif


#if(DEBUG_OUTPUT)
  LOG("STREAMER: Reading parameters\n");
#endif
#ifndef DAEMON
  clear_and_default(opt);
  err = parse_options(argc,argv,opt);
#endif
  if(err != 0)
    exit(-1);

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

  pthread_t rbuf_pthreads[opt->n_threads];
  pthread_t streamer_pthread;
  struct stats stats;

  opt->membranch = (struct entity_list_branch*)malloc(sizeof(struct entity_list_branch));
  opt->diskbranch = (struct entity_list_branch*)malloc(sizeof(struct entity_list_branch));

  opt->membranch->freelist = NULL;
  opt->membranch->busylist = NULL;
  opt->diskbranch->freelist = NULL;
  opt->diskbranch->busylist = NULL;

  pthread_mutex_init(&(opt->membranch->branchlock), NULL);
  pthread_mutex_init(&(opt->diskbranch->branchlock), NULL);
  pthread_cond_init(&(opt->membranch->busysignal), NULL);
  pthread_cond_init(&(opt->diskbranch->busysignal), NULL);

  //pthread_attr_t attr;
#ifdef TUNE_AFFINITY
  long processors = sysconf(_SC_NPROCESSORS_ONLN);
  D("Polled %ld processors",,processors);
  int cpusetter =1;
#endif

#ifdef TUNE_AFFINITY
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
#endif
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

  //Create message queue
  //pthread_mutex_init(&(optm.cumlock), NULL);
  //pthread_cond_init (&opt.signal, NULL);
  for(i=0;i<opt->n_drives;i++){
    struct recording_entity * re = (struct recording_entity*)malloc(sizeof(struct recording_entity));
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
	err = aiow_init_rec_entity(opt, re);
	//NOTE: elem_size is read inside if we're reading
	break;
#endif
      case REC_DUMMY:
	err = common_init_dummy(opt, re);
	break;
      case REC_DEF:
	err = def_init_def(opt, re);
	break;
      case REC_SPLICER:
	err = splice_init_splice(opt, re);
	break;
    }
    if(err != 0){
      LOGERR("Error in writer init\n");
      free(re);
      //exit(-1);
    }
    /* Add the recording entity to the diskbranch */
  }
  /* Check and set cfgs at this point */
#ifdef HAVE_LIBCONFIG_H
  err = init_cfg(opt);
  //TODO: cfg destruction
  if(err != 0){
    E("Error in cfg init");
    FREE_AND_ERROREXIT
  }
  //return -1;

  if(opt->optbits &READMODE){
    oper_to_all(opt->diskbranch,BRANCHOP_CHECK_FILES,(void*)opt);
    LOG("For recording %s: %lu files were found out of %lu total.\n", opt->filename, opt->cumul_found, opt->cumul);
  }
#endif
  /* If we're using the rx-ring, reserve space for it here */
  /*
     if(opt.optbits & USE_RX_RING){
     int flags = MAP_ANONYMOUS|MAP_SHARED;
     if(opt.optbits & USE_HUGEPAGE)
     flags |= MAP_HUGETLB;
     opt.buffer = mmap(NULL, ((unsigned long)sbuf->opt->buf_num_elems)*((unsigned long)sbuf->opt->buf_elem_size)*opt.n_threads, PROT_READ|PROT_WRITE , flags, 0,0);
     }
     */


#ifdef PRIORITY_SETTINGS

  rc = pthread_attr_getschedparam(&pta, &param);
  if(rc != 0)
    E("Error getting schedparam for pthread attr: %s",,strerror(rc));
  else
    D("Schedparam set to %d, Trying to set to minimun %d",, param.sched_priority, MIN_PRIO_FOR_PTHREAD);

  rc = pthread_attr_setschedpolicy(&pta, SCHED_FIFO);
  if(rc != 0)
    E("Error setting schedtype for pthread attr: %s",,strerror(rc));

  param.sched_priority = MIN_PRIO_FOR_PTHREAD;
  rc = pthread_attr_setschedparam(&pta, &param);
  if(rc != 0)
    E("Error setting schedparam for pthread attr: %s",,strerror(rc));
#endif
#if(DEBUG_OUTPUT)
  LOG("STREAMER: Initializing threads\n");
#endif
  for(i=0;i<opt->n_threads;i++){
    //int err = 0;
    struct buffer_entity * be = (struct buffer_entity*)malloc(sizeof(struct buffer_entity));
    /*
       struct listed_entity *le = (struct listed_entity*)malloc(sizeof(struct listed_entity));
       le->entity = (void*)be;
       add_to_entlist(&(opt.diskbranch), le);
       */
    //Make elements accessible
    //be->recer = re;
    //re->be = be;

    //Initialize recorder entity
    switch(opt->optbits & LOCKER_WRITER)
    {
      /*
	 case BUFFER_RINGBUF:
      //Helper function
      err = rbuf_init_buf_entity(&opt, be);
      break;
      */
      case BUFFER_SIMPLE:
	err = sbuf_init_buf_entity(opt,be);
	D("Initialized simple buffer for thread %d",,i);
	break;
      case WRITER_DUMMY:
	err = sbuf_init_buf_entity(opt, be);
	D("Initialized simple buffer for thread %d",,i);
	break;
    }
    if(err != 0){
      LOGERR("Error in buffer init\n");
      exit(-1);
    }
    //TODO: Change write loop to just loop. Now means both read and write
    D("Starting buffer thread");
#ifdef PRIORITY_SETTINGS
    rc = pthread_create(&rbuf_pthreads[i], &pta, be->write_loop,(void*)be);
#else
    rc = pthread_create(&rbuf_pthreads[i], NULL, be->write_loop,(void*)be);
#endif
#ifdef TUNE_AFFINITY
    if(cpusetter == processors)
      cpusetter = 1;
    CPU_SET(cpusetter,&cpuset);
    cpusetter++;

    D("Tuning buffer thread %i to processor %i",,i,cpusetter);
    rc = pthread_setaffinity_np(rbuf_pthreads[i], sizeof(cpu_set_t), &cpuset);
    if(rc != 0){
      perror("Affinity");
      E("Error: setting affinity");
    }
    CPU_ZERO(&cpuset);
#endif
  }

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

  //be->se = &(threads[i]);
  //threads[i].be = be;

  //TODO: Packet index recovery 
  /*
     if(opt.optbits & READMODE){
  //unsigned long total_packets = 0;
  opt.max_num_packets =0 ;
  for(i=0;i<opt.n_threads;i++)
  //TODO: Fix sendside
  //opt.max_num_packets += threads[i].be->recer->get_n_packets(threads[i].be->recer);

  }
  */

  /* Just start the one receiver */
  //for(i=0;i<opt.n_threads;i++){
  //#if(DEBUG_OUTPUT)
  printf("STREAMER: In main, starting receiver thread \n");
  //#endif

#ifdef PRIORITY_SETTINGS
  param.sched_priority = MAX_PRIO_FOR_PTHREAD;
  rc = pthread_attr_setschedparam(&pta, &param);
  if(rc != 0)
    E("Error setting schedparam for pthread attr: %s, to %d",, strerror(rc), MAX_PRIO_FOR_PTHREAD);
  rc = pthread_create(&streamer_pthread, &pta, streamer_ent.start, (void*)&streamer_ent);
#else
  rc = pthread_create(&streamer_pthread, NULL, streamer_ent.start, (void*)&streamer_ent);
#endif
  if (rc != 0){
    printf("ERROR; return code from pthread_create() is %d\n", rc);
    exit(-1);
  }
#ifdef TUNE_AFFINITY
  /* Put the capture on the first core */
  CPU_SET(0,&cpuset);
  /*
     cpusetter++;
     if(cpusetter > processors)
     cpusetter = 1;
     */

  rc = pthread_setaffinity_np(streamer_pthread, sizeof(cpu_set_t), &cpuset);
  if(rc != 0){
    E("Error: setting affinity: %d",,rc);
  }
  CPU_ZERO(&cpuset);
#endif
  //Spread processes out to n cores
  //NOTE: setaffinity should be used after thread has been started

  //}

  init_stat(&stats);
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
    struct stats stats_prev, stats_now;//, stats_temp;
    int sleeptodo;
    memset(&stats_prev, 0,sizeof(struct stats));
    //memset(&stats_now, 0,sizeof(struct stats));
    LOG("STREAMER: Printing stats per second\n");
    LOG("----------------------------------------\n");

    if(opt->optbits & READMODE)
      sleeptodo= 1;
    else
      sleeptodo = opt->time;
    while(sleeptodo >0 && streamer_ent.is_running(&streamer_ent)){
      sleep(1);
      memset(&stats_now, 0,sizeof(struct stats));

      streamer_ent.get_stats(streamer_ent.opt, &stats_now);
      /* Query and print the stats */
      /*
	 for(i=0;i<opt.n_threads;i++){
      //threads[i].get_stats(threads[i].opt, &stats_now);
      if(threads[i].be->recer->get_stats != NULL)
      threads[i].be->recer->get_stats(threads[i].be->recer->opt, &stats_now);
      }
      */
      //TODO: Write end stats
      oper_to_all(opt->diskbranch,BRANCHOP_GETSTATS,(void*)&stats_now);

      //memcpy(&stats_temp, &stats_now, sizeof(struct stats));
      neg_stats(&stats_now, &stats_prev);

      print_intermediate_stats(&stats_now);
      //LOG("Time %ds \t------------------------\n", opt.time-sleeptodo+1);
      if(!(opt->optbits & READMODE))
	LOG("Time %lds\n", opt->time-sleeptodo+1);
      else
	LOG("Time %ds\n", sleeptodo);

      LOG("Ringbuffers: ");
      print_br_stats(opt->membranch);
      LOG("Recpoints: ");
      print_br_stats(opt->diskbranch);

      LOG("----------------------------------------\n");

      if(!(opt->optbits & READMODE))
	sleeptodo--;
      else
	sleeptodo++;
      add_stats(&stats_prev, &stats_now);
      //memcpy(&stats_prev, &stats_temp, sizeof(struct stats));
      /*
	 if(opt.optbits & READMODE){
	 if(opt.cumul >= opt.max_num_packets-1)
	 sleeptodo = 0;
	 }
	 */
    }
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
  rc = pthread_join(streamer_pthread, NULL);
  if (rc<0) {
    printf("ERROR; return code from pthread_join() is %d\n", rc);
  }
  else
    D("Streamer thread exit OK");

  // Stop the memory threads 
  oper_to_all(opt->membranch, BRANCHOP_STOPANDSIGNAL, NULL);
  int k = 0;
  for(i =0 ;i<opt->n_threads;i++){
    rc = pthread_join(rbuf_pthreads[i], NULL);
    if (rc<0) {
      printf("ERROR; return code from pthread_join() is %d\n", rc);
    }
    else
      D("%dth buffer exit OK",,k);
    k++;
  }
  D("Getting stats and closing");

  streamer_ent.close(streamer_ent.opt, (void*)&stats);
  oper_to_all(opt->membranch, BRANCHOP_CLOSERBUF, (void*)&stats);
  oper_to_all(opt->diskbranch, BRANCHOP_CLOSEWRITER, (void*)&stats);

#ifdef HAVE_LIBCONFIG_H
  //TODO: This will be destroyed outside the mem and diskbranches
  config_destroy(&(opt->cfg));
#endif
  //oper_to_all(opt.diskbranch,BRANCHOP_GETSTATS,(void*)&stats);

#if(DEBUG_OUTPUT)
  LOG("STREAMER: Threads finished. Getting stats\n");
#endif
  print_stats(&stats, opt);

  /*Close everything */

  //}
  /* Log the time */
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
  //Close all threads. Buffers and writers are closed in the threads close
  //for(i=0;i<opt.n_threads;i++){
  //threads[i].close(threads[i].opt, &stats);
  //TODO Free this stuff elsewhere
  //free(threads[i].be->recer);
  //free(threads[i].be);
  //}
  //pthread_mutex_destroy(&(opt.cumlock));
  //free(opt.packet_index);
  for(i=0;i<opt->n_drives;i++){
    //opt.filenames[i] = malloc(sizeof(char)*FILENAME_MAX);
    free(opt->filenames[i]);
    //opt.filenames[i] = (char*)malloc(FILENAME_MAX);
  }
#if(DEBUG_OUTPUT)
  LOG("STREAMER: Threads closed\n");
#endif
  if(opt->device_name != NULL)
    free(opt->device_name);
  if(opt->optbits & READMODE){
    if(opt->fileholders != NULL)
      free(opt->fileholders);
  }
  if(opt->cfgfile != NULL)
    free(opt->cfgfile);
  config_destroy(&(opt->cfg));
  free(opt->membranch);
  free(opt->diskbranch);
  pthread_attr_destroy(&pta);
#ifndef DAEMON
  free(opt);
#endif

  //return 0;
  //pthread_exit(NULL);
  return 0;
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
/*
int update_cfg(struct opt_s* opt, struct config_t * cfg){
  if (cfg == NULL)
    cfg = &(opt->cfg);
  int err;
  D("Updating CFG");
  config_setting_t *root, *setting;
  root = config_root_setting(cfg);
  CHECK_ERR_NONNULL(root, "getroot");
  if(opt->optbits & READMODE){
    unsigned long filesize=0;
    GET_I64("cumul",opt->cumul);
    GET_I64("buf_elem_size", opt->buf_elem_size);
    GET_I64("filesize", filesize);
    GET_I64("total_packets", opt->total_packets);
    GET_I("buf_division", opt->buf_division);
    opt->buf_num_elems = filesize/opt->buf_elem_size;
    opt->do_w_stuff_every = filesize/((unsigned long)opt->buf_division);
  }
  else{
    SET_I64("cumul",opt->cumul);
    SET_I64("buf_elem_size", opt->buf_elem_size);
    SET_I64("filesize", (opt->buf_elem_size)*(opt->buf_num_elems));
    SET_I("buf_division", opt->buf_division);
    SET_I64("total_packets", opt->total_packets);
  }
  //err = config_lookup_int64(&opt->cfg, "cumul", &cumul);
  //CHECK_CFG("get cumul");
  D("CFG updated");
  return 0;
}
*/
