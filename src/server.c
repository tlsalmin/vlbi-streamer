#include <sys/inotify.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <libconfig.h>
#include <string.h> /* MEMSET */

#include "config.h"
#include "streamer.h"
#define CFGFILE SYSCONFDIR "/vlbistreamer.conf"
#define STATEFILE LOCALSTATEDIR "/opt/vlbistreamer/schedule"
#define LOGFILE	LOCALSTATEDIR "/log/vlbistreamer.log"
#define MAX_SCHEDULED 512
#define MAX_RUNNING 4
#define SECS_TO_START_IN_ADVANCE 10
#define MAX_INOTIFY_EVENTS MAX_SCHEDULED
//stuff stolen from http://darkeside.blogspot.fi/2007/12/linux-inotify-example.html
#define BUFF_SIZE ((sizeof(struct inotify_event)+FILENAME_MAX)*MAX_INOTIFY_EVENTS)

struct scheduled_event{
  struct opt_s * opt;
  struct scheduled_event* next;
  int found;
};
/* Just to cut down on number of variables passed to functions		*/
struct schedule{
  struct scheduled_event* scheduled_head;
  struct scheduled_event* running_head;
  struct opt_s * default_opt;
  int n_scheduled;
  int n_running;
};
inline void zero_sched(struct schedule *sched){
  sched->scheduled_head = NULL;
  sched->running_head = NULL;
  sched->default_opt = NULL;
  sched->n_scheduled = 0;
  sched->n_running =0;
}
inline struct scheduled_event* get_from_head(char * name, struct scheduled_event* head){
  while(head != NULL){
    if(strcmp(head->opt->filename, name) == 0){
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
      /* Note: Below line makes presumptions on call order.	*/
      event->found = 0;
    }
  }
  return NULL;
}
int add_recording(config_setting_t* root, struct schedule* sched)
{
  (void)root;
  (void)sched;
  /* Hmm set found to 1 here, or in check_schedule.. */
  return 0;
}
int remove_recording(struct scheduled_event *ev, struct schedule *sched){
  (void)ev;
  (void)sched;
  return 0;
}
int check_schedule(struct schedule *sched){
  int i=0,err;
  config_t cfg;
  struct scheduled_event * temp = NULL;
  config_init(&cfg);
  config_read_file(&cfg, STATEFILE);
  config_setting_t *root, *setting;

  root = config_root_setting(&cfg);
  setting = config_setting_get_elem(root,i);
  /* Go through all the scheduled recordings	 			*/
  /* If theres a new one: Add it to the scheduled			*/
  /* If one that was there is now missing, remove it from schedule	*/
  /* else just set the events found to 1				*/
  for(i=0;setting != NULL;setting=config_setting_get_elem(root,i)){
    temp = get_event_by_name(config_setting_name(setting), sched);
    /* New scheduled recording! 					*/
    if(temp == NULL){
      err = add_recording(setting, sched);
      CHECK_ERR("Add recording");
    }
    else
      temp->found = 1;
  }
  while((temp = get_not_found(sched->scheduled_head)) != NULL){
    LOG("Recording %s removed from schedule\n", temp->opt->filename);
    err =  remove_recording(temp, sched);
    CHECK_ERR("Remove recording");
  }
  while((temp = get_not_found(sched->running_head)) != NULL){
    LOG("Recording %s removed from running\n", temp->opt->filename);
    err =  remove_recording(temp, sched);
    CHECK_ERR("Remove recording");
  }
    
  return 0;
}
int main(int argc, char **argv)
{
  int err,i_fd,w_fd,is_running = 1;

  struct schedule *sched = malloc(sizeof(struct schedule));
  //memset((void*)&sched, 0,sizeof(struct schedule));
  zero_sched(sched);

  sched->default_opt = malloc(sizeof(struct opt_s));
  char ibuff[BUFF_SIZE] = {0};

  /* First load defaults to opts, then check default config file	*/
  /* and lastly check command line arguments. This means config file	*/
  /* overrides defaults and command line arguments override config file	*/
  clear_and_default(sched->default_opt);

  sched->default_opt->cfgfile = CFGFILE;
  err = read_full_cfg(sched->default_opt);
  CHECK_ERR("Load default cfg");

  parse_options(argc,argv,sched->default_opt);

  i_fd = inotify_init();
  CHECK_LTZ("Inotify init", i_fd);

  D("Starting watch on statefile %s",, STATEFILE);
  w_fd = inotify_add_watch(i_fd, STATEFILE, IN_MODIFY);
  CHECK_LTZ("Add watch",w_fd);

  /* Make the inotify nonblocking for simpler loop 			*/
  int flags = fcntl(i_fd, F_GETFL, 0);
  fcntl(i_fd, F_SETFL, flags |O_NONBLOCK);

  /* Initialize rec points						*/
  /* TODO: First make filewatching work! 				*/


  while(is_running)
  {
    /* Check for changes 						*/
    if (read(i_fd, ibuff, BUFF_SIZE) != 0)
      {
	/* There's really just one sort of event. Adding or removing	*/
	/* a scheduled recording					*/
	err = check_schedule(sched);
	CHECK_ERR("Checked schedule");
      }
    /*Remove when ready to test properly				*/
    is_running = 0;
  }

  inotify_rm_watch(i_fd, w_fd);
  free(sched->default_opt);
  free(sched);
  return 0;
}
