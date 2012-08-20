#include <sys/inotify.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <libconfig.h>
#include <string.h> /* MEMSET */
#include <poll.h>

#include "config.h"
#include "streamer.h"
#include "confighelper.h"
#define CFGFILE SYSCONFDIR "/vlbistreamer.conf"
#define STATEFILE LOCALSTATEDIR "/opt/vlbistreamer/schedule"
#define LOGFILE	LOCALSTATEDIR "/log/vlbistreamer.log"
#define MAX_SCHEDULED 512
#define MAX_RUNNING 4
#define SECS_TO_START_IN_ADVANCE 1
#define MAX_INOTIFY_EVENTS MAX_SCHEDULED
#define IDSTRING_LENGTH 24
#define CHAR_BUFF_SIZE ((sizeof(struct inotify_event)+FILENAME_MAX)*MAX_INOTIFY_EVENTS)

struct scheduled_event{
  struct opt_s * opt;
  struct scheduled_event* next;
  struct stats* stats;
  char * idstring;
  void (*shutdown_thread)(struct opt_s*);
  pthread_t pt;
  int found;
};
/* Just to cut down on number of variables passed to functions		*/
struct schedule{
  struct scheduled_event* scheduled_head;
  struct scheduled_event* running_head;
  struct opt_s * default_opt;
  int n_scheduled;
  int n_running;
  int running;
};
inline void zero_sched(struct schedule *sched){
  /*
  memset(sched,0,sizeof(sched));
    */
  sched->scheduled_head = NULL;
  sched->running_head = NULL;
  sched->default_opt = NULL;
  sched->n_scheduled = 0;
  sched->n_running =0;
}
inline void zero_schedevnt(struct scheduled_event* ev){
  ev->opt = NULL;
  ev->next =NULL;
  ev->pt =0;
  ev->found=0;
    }
/*
int set_running(struct schedule *sched, struct scheduled_event * ev, struct scheduled_event *parent){
  struct scheduled_event * temp;
  if(parent == NULL){
    sched->scheduled_head = ev->next;
  }
  else{
    temp = sched->scheduled_hea
    while(
  }

}
*/
int remove_from_cfgsched(struct scheduled_event *ev){
  int err;
  config_t cfg;
  config_init(&cfg);
  config_read_file(&cfg, STATEFILE);
  config_setting_t *root;

  root = config_root_setting(&cfg);
  err = config_setting_remove(root, ev->idstring);
  CHECK_CFG("remove closed recording");
  err = config_write_file(&cfg, STATEFILE);
  CHECK_CFG("Wrote config");
  LOG("Updated config file and removed %s\n", ev->opt->filename);
  config_destroy(&cfg);
  return 0;
}
int free_and_close(struct scheduled_event *ev){
  D("optstatus: %d",, ev->opt->status);
  int err;
  if(ev->opt->status == STATUS_FINISHED){
    LOG("Recording %s finished OK\n",ev->opt->filename);
  }
  else if(ev->opt->status == STATUS_ERROR){
    LOG("Recording %s finished in ERROR\n",ev->opt->filename);
  }
  else if(ev->opt->status == STATUS_RUNNING){
    D("Still running");
    if(ev->shutdown_thread != NULL){
      ev->shutdown_thread(ev->opt);
      D("Thread shut down");
    }
  //TODO: Cancelling threads running etc.
  }
  else{
    if(ev->shutdown_thread != NULL){
      ev->shutdown_thread(ev->opt);
      D("Thread shut down");
    }
    LOG("Recording %s cancelled\n",ev->opt->filename);
    ev->opt->status = STATUS_CANCELLED;
    //TODO: cancellation
  }
    err = pthread_join(ev->pt, NULL);
    CHECK_ERR("join");
    err = close_streamer(ev->opt);
    CHECK_ERR("close stream");
  err = remove_from_cfgsched(ev);
  /* Not a critical error */
  if(err !=0)
    E("Removing cfg entry from sched");
  free(ev->idstring);
  close_opts(ev->opt);
  free(ev);
  return 0;
}
int remove_recording(struct scheduled_event *ev, struct scheduled_event **head){
  int found = 0;
  struct scheduled_event *temp;
  struct scheduled_event *parent;
  if(*head == ev){
    *head = (*head)->next;
    found=1;
    D("Removed schedevent");
    //return 0;
  }
  else{
    parent = *head;
    temp = parent->next;
    while(temp != NULL){
      if(temp == ev){
	found=1;
	parent->next = temp->next;
	D("Removed schedevent");
	break;
	//return 0;
      }
      else{
	parent = temp;
	temp = parent->next;
      }
    }
  }
  if(found){
    D("Free and close it");
    free_and_close(ev);
    return 0;
  }
  D("Didn't find schedevent in branch");
  return -1;
}
inline void add_to_end(struct scheduled_event ** head, struct scheduled_event * to_add){
  if(*head == NULL)
    *head = to_add;
  else{
    struct scheduled_event * temp = *head;
    while(temp->next != NULL)
      temp = temp->next;
    temp->next = to_add;
  }
}
inline void change_sched_branch(struct scheduled_event **from, struct scheduled_event ** to, struct scheduled_event *ev){

  if(*from == ev){
    *from = ev->next;
    ev->next = NULL;
  }
  else{
    struct scheduled_event * temp = *from;
    while(temp->next != ev){
      temp = temp->next;
    }
    temp->next = ev->next;
  }
  /* If we want to use this for removing */
  if(to != NULL)
    add_to_end(to, ev);
}
int start_event(struct scheduled_event *ev){
  int err;
  ev->opt->status = STATUS_RUNNING;
  if(ev->opt->optbits & VERBOSE){
    ev->stats = (struct stats*)malloc(sizeof(struct stats));
    init_stats(ev->stats);
  }
  err = pthread_create(&ev->pt, NULL, vlbistreamer, (void*)ev->opt); 
  CHECK_ERR("streamer thread create");
  /* TODO: check if packet size etc. match original config */
  return 0;
}
int start_scheduled(struct schedule *sched){
  int err=0;
  int tdif;
  TIMERTYPE time_now;
  GETTIME(time_now);
  struct scheduled_event * ev;
  //struct scheduled_event * parent = NULL;
  for(ev = sched->scheduled_head;ev != NULL;){
    tdif = get_sec_diff(&time_now, &ev->opt->starting_time);
    if(ev->opt->starting_time.tv_sec == 0 || tdif <= SECS_TO_START_IN_ADVANCE){
      struct scheduled_event * ev_temp = ev;
      ev = ev->next;
      /* Removes old stuff. IF tv_sec is 0, should start immediately */
      if(ev_temp->opt->starting_time.tv_sec != 0 && (!(ev_temp->opt->optbits & READMODE) && (tdif < -((long)ev_temp->opt->time)))){
	LOG("Removing clearly too old recording request %s, which should have started %d seconds ago\n", ev_temp->opt->filename, -tdif);
	LOG("Start time %lu\n", ev_temp->opt->starting_time.tv_sec);
	remove_recording(ev_temp,&(sched->scheduled_head));
	continue;
      }
      LOG("Starting event %s\n", ev_temp->opt->filename);
      sched->n_scheduled--;
      err = start_event(ev_temp);
      if(err != 0){
	E("Something went wrong in recording %s start",, ev_temp->opt->filename);
	remove_recording(ev_temp,&(sched->scheduled_head));
	}
      else{
	change_sched_branch(&sched->scheduled_head, &sched->running_head, ev_temp);
	sched->n_running++;
      }
      //set_running(sched, ev, parent);
    }
    else
      ev = ev->next;
    //parent = ev;
  }
  return 0;
}
inline struct scheduled_event* get_from_head(char * name, struct scheduled_event* head){
  while(head != NULL){
    if(strcmp(head->idstring, name) == 0){
      return head;
    }
    head = head->next;
  }
  return NULL;
}
inline struct scheduled_event* get_event_by_name(char * name, struct schedule* sched){
  struct scheduled_event* returnable = NULL;
  returnable = get_from_head(name, sched->scheduled_head);
  if(returnable == NULL)
    returnable = get_from_head(name, sched->running_head);
  return returnable;
}
inline struct scheduled_event* get_not_found(struct scheduled_event* event){
  while(event != NULL){
    if(event->found == 0)
      return event;
    else{
      event = event->next;
    }
  }
  return NULL;
}
int add_recording(config_setting_t* root, struct schedule* sched)
{
  D("Adding new schedevent");
  int err;
  if(strcmp(config_setting_name(root), "shutdown") == 0){
    LOG("Shutdown scheduled\n");
    config_t cfg;
    config_setting_t *realroot;
    config_init(&cfg);
    config_read_file(&cfg, STATEFILE);

    realroot = config_root_setting(&cfg);
    err = config_setting_remove(realroot, "shutdown");
    CHECK_CFG("remove shutdown command");
    err = config_write_file(&cfg, STATEFILE);
    CHECK_CFG("Wrote config");
    sched->running = 0;
  
    struct scheduled_event * temp = sched->running_head;
    while(temp != NULL){
      temp->shutdown_thread(temp->opt);
      temp = temp->next;
      D("Threads shut down");
    }

    config_destroy(&cfg);
    D("Shutdown finished");
    return 0;
  }
  struct scheduled_event * se = (struct scheduled_event*)malloc(sizeof(struct scheduled_event));
  CHECK_ERR_NONNULL(se, "Malloc scheduled event");
  struct opt_s *opt = malloc(sizeof(struct opt_s));
  CHECK_ERR_NONNULL(opt, "Opt for event malloc");
  se->shutdown_thread = shutdown_thread;

  se->found=1;
  se->next=NULL;
  se->opt = opt;
  /* Copy the default opt over our opt	*/
  clear_and_default(opt,0);
  /* membranch etc. should be copied here also */
  memcpy(opt,sched->default_opt, sizeof(struct opt_s));

  clear_pointers(opt);
  config_init(&(opt->cfg));

  /* Get the name of the recording	*/
  opt->filename = (char*)malloc(sizeof(char)*FILENAME_MAX);
  se->idstring = (char*)malloc(sizeof(char)*24);
  CHECK_ERR_NONNULL(opt->filename, "Filename for opt malloc");
  //strcpy(opt->filename, config_setting_name(root));
  strcpy(se->idstring, config_setting_name(root));

  LOG("Adding new request with id string: %s\n", se->idstring);

  /* Get the rest of the opts		*/
  err = set_from_root(opt, root, 0,0);
  if(err != 0){
    E("Broken schedule config. Not scheduling %s",, se->idstring);
    free_and_close(se);
    return 0;
  }

  /* Null here, set in initializer. */
  opt->get_stats = NULL;

  LOG("New request is for session: %s\n", opt->filename);
  D("Opts checked, port is %d",, opt->port);
  //config_init(&(opt->cfg));

  //Special case if some old buggers in sched file
  //TODO: Check packet sizes and handle buffers accordingly

  add_to_end(&(sched->scheduled_head), se);
  D("Schedevent added");
  return 0;
}
void zerofound(struct schedule *sched){
  struct scheduled_event * temp;
  for(temp = sched->scheduled_head; temp != NULL; temp=temp->next)
    temp->found = 0;
  for(temp = sched->running_head; temp != NULL; temp=temp->next)
    temp->found = 0;
}
int check_schedule(struct schedule *sched){
  int i=0,err;
  struct scheduled_event * temp = NULL;
  config_t cfg;
  config_init(&cfg);
  config_read_file(&cfg, STATEFILE);
  config_setting_t *root, *setting;

  root = config_root_setting(&cfg);
  setting = config_setting_get_elem(root,i);

  D("Checking schedule");
  /* Go through all the scheduled recordings	 			*/
  /* If theres a new one: Add it to the scheduled			*/
  /* If one that was there is now missing, remove it from schedule	*/
  /* else just set the events found to 1				*/
  for(setting=config_setting_get_elem(root,i);setting != NULL;setting=config_setting_get_elem(root,++i)){
    temp = get_event_by_name(config_setting_name(setting), sched);
    /* New scheduled recording! 					*/
    if(temp == NULL){
      D("New schedule event found");
      err = add_recording(setting, sched);
      CHECK_ERR("Add recording");
    }
    else{
      D("Found ye olde");
      temp->found = 1;
    }
  }
  while((temp = get_not_found(sched->scheduled_head)) != NULL){
    LOG("Recording %s removed from schedule\n", temp->opt->filename);
    err =  remove_recording(temp, &(sched->scheduled_head));
    CHECK_ERR("Remove recording");
  }
  while((temp = get_not_found(sched->running_head)) != NULL){
    LOG("Recording %s removed from running\n", temp->opt->filename);
    //TODO: Stop the recording
    err =  remove_recording(temp, &(sched->running_head));
    CHECK_ERR("Remove recording");
    sched->n_running--;
  }
  zerofound(sched);
  config_destroy(&cfg);
  D("Done checking schedule");

  return 0;
}
/*
int close_recording(struct schedule* sched, struct scheduled_event* ev){
  int err;
  err =pthread_join(ev->pt, NULL);
  CHECK_ERR("pthread tryjoin");
  D("Thread for %s finished",,ev->opt->filename);
  change_sched_branch(&(sched->running_head), NULL, ev);

  err = remove_from_cfgsched(ev);
  CHECK_ERR("Remove from cfgsched");
  close_streamer(ev->opt);
  close_opts(ev->opt);
  free(ev);
  return 0;
}
*/
int check_finished(struct schedule* sched){
  struct scheduled_event *ev;
  int err;
  for(ev = sched->running_head;ev!=NULL;){
    struct scheduled_event *temp = ev->next;
    if(ev->opt->status & (STATUS_FINISHED | STATUS_ERROR |STATUS_CANCELLED)){
      //err = close_recording(sched, ev);
      if(ev->opt->optbits & VERBOSE)
	free(ev->stats);
      err = remove_recording(ev, &(sched->running_head));
      CHECK_ERR("close recording");
      sched->n_running--;
    }
    ev = temp;
  }
  return 0;
}
int main(int argc, char **argv)
{
  int err,i_fd,w_fd;

  struct stats* tempstats = NULL;//, stats_temp;

  struct schedule *sched = malloc(sizeof(struct schedule));
  TIMERTYPE *temptime = NULL;
  CHECK_ERR_NONNULL(sched, "Sched malloc");
  //memset((void*)&sched, 0,sizeof(struct schedule));
  zero_sched(sched);
  struct stats* stats_full = (struct stats*)malloc(sizeof(struct stats));
  CHECK_ERR_NONNULL(stats_full, "stats malloc");

  sched->default_opt = malloc(sizeof(struct opt_s));
  CHECK_ERR_NONNULL(sched->default_opt, "Default opt malloc");
  char *ibuff = (char*)malloc(sizeof(char)*CHAR_BUFF_SIZE);
  CHECK_ERR_NONNULL(ibuff, "ibuff malloc");
  memset(ibuff, 0,sizeof(ibuff));

  /* First load defaults to opts, then check default config file	*/
  /* and lastly check command line arguments. This means config file	*/
  /* overrides defaults and command line arguments override config file	*/
  clear_and_default(sched->default_opt,1);

  LOG("Running in daemon mode\n");

//stuff stolen from http://darkeside.blogspot.fi/2007/12/linux-inotify-example.html
  sched->default_opt->cfgfile = (char*)malloc(sizeof(char)*FILENAME_MAX);
  CHECK_ERR_NONNULL(sched->default_opt->cfgfile, "cfgfile malloc");
  sprintf(sched->default_opt->cfgfile, "%s", CFGFILE);
  //sched->default_opt->cfgfile = CFGFILE;
  err = read_full_cfg(sched->default_opt);
  CHECK_ERR("Load default cfg");
  LOG("Read config from %s\n", sched->default_opt->cfgfile);

  parse_options(argc,argv,sched->default_opt);

  /* Start memory buffers */
  LOG("Prepping recpoints and membuffers..");
  err = init_branches(sched->default_opt);
  CHECK_ERR("init branches");
  err = init_recp(sched->default_opt);
  CHECK_ERR("init recpoints");
  err = init_rbufs(sched->default_opt);
  CHECK_ERR("init rbufs");

  i_fd = inotify_init();
  CHECK_LTZ("Inotify init", i_fd);

  LOG("Starting watch on statefile %s\n", STATEFILE);
  w_fd = inotify_add_watch(i_fd, STATEFILE, IN_MODIFY);
  CHECK_LTZ("Add watch",w_fd);

  /* Make the inotify nonblocking for simpler loop 			*/
  //int flags = fcntl(i_fd, F_GETFL, 0);
  //fcntl(i_fd, F_SETFL, flags |O_NONBLOCK);
  
  /* Initialize rec points						*/
  /* TODO: First make filewatching work! 				*/
  struct pollfd * pfd = (struct pollfd*)malloc(sizeof(pfd));
  CHECK_ERR_NONNULL(pfd, "pollfd malloc");
  memset(pfd, 0,sizeof(pfd));
  pfd->fd = i_fd;
  pfd->events = POLLIN|POLLERR;

  sched->running = 1;

  /* Initial check before normal loop */
  LOG("Checking initial schedule\n");
  err = check_schedule(sched);
  CHECK_ERR("Checked schedule");

  if(sched->default_opt->optbits & VERBOSE){
    tempstats = (struct stats*)malloc(sizeof(struct stats));
    CHECK_ERR_NONNULL(tempstats, "stats malloc");
    init_stats(tempstats);
    LOG("STREAMER: Printing stats per second\n");
    LOG("----------------------------------------\n");
    temptime = (TIMERTYPE*)malloc(sizeof(TIMERTYPE));
  }

  LOG("Running..\n");
  while(sched->running == 1)
  {
    err = start_scheduled(sched);
    CHECK_ERR("Start scheduled");
    /* Check for changes 						*/
    err = poll(pfd, 1, 1000);
    if(err < 0){
      perror("Poll");
      sched->running = 0;
    }
    else if(err >0)
    {
      err = read(i_fd, ibuff, CHAR_BUFF_SIZE);
      CHECK_ERR_LTZ("Read schedule");
      D("Noticed change in schedule file");
      /* There's really just one sort of event. Adding or removing	*/
      /* a scheduled recording					*/
      err = check_schedule(sched);
      CHECK_ERR("Checked schedule");
    }
    else
      D("Nothing happened on schedule file");
    err = check_finished(sched);
    CHECK_ERR("check finished");
    if(sched->default_opt->optbits & VERBOSE && sched->n_running > 0){
      struct scheduled_event * le = sched->running_head;
      GETTIME(*temptime);
      LOG("Time:\t%lu\n", temptime->tv_sec);
      while(le != NULL){
	if(le->opt->get_stats != NULL){
	  init_stats(tempstats);
	  le->opt->get_stats((void*)le->opt, (void*)tempstats);
	  neg_stats(tempstats, le->stats);
	  LOG("Event:\t%s\t", le->opt->filename);
	  LOG("Network:\t%luMb/s\tDropped %lu\tIncomplete %lu\n"
	      ,BYTES_TO_MBITSPS(tempstats->total_bytes),tempstats->dropped, tempstats->incomplete);
	  add_stats(le->stats, tempstats);
	}
	le = le->next;
      }
      init_stats(tempstats);
      oper_to_all(sched->default_opt->diskbranch,BRANCHOP_GETSTATS,(void*)tempstats);
      neg_stats(tempstats, stats_full);
      LOG("HD-Speed:\t%luMB/s\n",BYTES_TO_MBITSPS(tempstats->total_written));
      add_stats(stats_full, tempstats);

      LOG("Ringbuffers: ");
      print_br_stats(sched->default_opt->membranch);
      LOG("Recpoints: ");
      print_br_stats(sched->default_opt->diskbranch);

      LOG("----------------------------------------\n");
      fflush(stdout);
    }

    fflush(stdout);
  }
  if(sched->default_opt->optbits & VERBOSE){
    free(tempstats);
    free(temptime);
  }

  inotify_rm_watch(i_fd, w_fd);

  D("Closing membranch and diskbranch");
  close_rbufs(sched->default_opt, stats_full);
  D("Membranch closed");
  close_recp(sched->default_opt,stats_full);
  D("Membranch and diskbranch shut down");

  //TODO: Full stats not used yet
  free(stats_full);
  free(pfd);
  free(ibuff);
  D("Poll device closed");
  close_opts(sched->default_opt);
  D("Closed default opt");
  //free(sched->default_opt);
  free(sched);
  D("Schedule freed");
  return 0;
}
