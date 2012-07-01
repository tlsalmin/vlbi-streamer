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
  add_to_end(to, ev);
}
int start_event(struct scheduled_event *ev){
  int err;
  err = pthread_create(&ev->pt, NULL, vlbistreamer, (void*)ev->opt); 
  CHECK_ERR("streamer thread create");
  (void)ev;
  return 0;
}
int start_scheduled(struct schedule *sched){
  int err=0;
  TIMERTYPE time_now;
  GETTIME(time_now);
  struct scheduled_event * ev;
  //struct scheduled_event * parent = NULL;
  for(ev = sched->scheduled_head;ev != NULL;ev = ev->next){
    if(get_sec_diff(&time_now, &ev->opt->starting_time)< SECS_TO_START_IN_ADVANCE){
      D("Starting event %s",, ev->opt->filename);
      sched->n_scheduled--;
      err = start_event(ev);
      CHECK_ERR("Start event");
      change_sched_branch(&sched->scheduled_head, &sched->running_head, ev);
      sched->n_running++;
      //set_running(sched, ev, parent);
    }
    //parent = ev;
    ev = ev->next;
  }
  return 0;
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
    }
  }
  return NULL;
}
int add_recording(config_setting_t* root, struct schedule* sched)
{
  D("Adding new schedevent");
  //int err;
  struct scheduled_event * se = (struct scheduled_event*)malloc(sizeof(struct scheduled_event));
  struct opt_s *opt = malloc(sizeof(struct opt_s));

  se->found=1;
  se->next=NULL;
  se->opt = opt;
  /* Copy the default opt over our opt	*/
  clear_and_default(opt);
  /* membranch etc. should be copied here also */
  memcpy(opt,sched->default_opt, sizeof(struct opt_s));

  /* Get the name of the recording	*/
  opt->filename = (char*)malloc(sizeof(char)*FILENAME_MAX);
  strcpy(opt->filename, config_setting_name(root));
  D("Schedevent is named: %s",, opt->filename);

  /* Get the rest of the opts		*/
  set_from_root(opt, root, 0,0);
  //config_init(&(opt->cfg));

  //TODO: Check packet sizes and handle buffers accordingly

  struct scheduled_event * temp = sched->scheduled_head;
  if(temp !=NULL){
    while(temp->next != NULL)
      temp = temp->next;
  }
  temp->next = se;
  D("Schedevent added");
  return 0;
}
int remove_recording(struct scheduled_event *ev, struct scheduled_event **head){
  struct scheduled_event *temp;
  struct scheduled_event *parent;
  if(*head == ev){
    *head = (*head)->next;
    D("Removed schedevent");
    return 0;
  }
  else{
    parent = *head;
    temp = parent->next;
    while(temp != NULL){
      if(temp == ev){
	parent->next = temp->next;
	D("Removed schedevent");
	return 0;
      }
      else{
	parent = temp;
	temp = parent->next;
      }
    }
  }
  D("Didn't find schedevent in branch");
  return -1;
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
  config_t cfg;
  struct scheduled_event * temp = NULL;
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
  for(i=0;setting != NULL;setting=config_setting_get_elem(root,i)){
    temp = get_event_by_name(config_setting_name(setting), sched);
    /* New scheduled recording! 					*/
    if(temp == NULL){
      D("New schedule event found");
      err = add_recording(setting, sched);
      CHECK_ERR("Add recording");
    }
    else
      temp->found = 1;
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
  }
  zerofound(sched);

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
  //int flags = fcntl(i_fd, F_GETFL, 0);
  //fcntl(i_fd, F_SETFL, flags |O_NONBLOCK);

  /* Initialize rec points						*/
  /* TODO: First make filewatching work! 				*/
  struct pollfd * pfd = (struct pollfd*)malloc(sizeof(pfd));
  memset(pfd, 0,sizeof(pfd));
  pfd->fd = i_fd;
  pfd->events = POLLIN|POLLERR;

  /* Initial check before normal loop */
  err = check_schedule(sched);
  CHECK_ERR("Checked schedule");


  while(is_running)
  {
    err = start_scheduled(sched);
    CHECK_ERR("Start scheduled");
    /* Check for changes 						*/
    err = poll(pfd, 1, 5000);
    if(err < 0){
      perror("Poll");
      is_running = 0;
    }
    else if(err >0)
    {
      read(i_fd, ibuff, BUFF_SIZE);
      D("Noticed change in schedule file");
      /* There's really just one sort of event. Adding or removing	*/
      /* a scheduled recording					*/
      err = check_schedule(sched);
      CHECK_ERR("Checked schedule");
    }
    else
      D("Nothing happened on schedule file");
    /*Remove when ready to test properly				*/
    //sleep(1);
  }

  inotify_rm_watch(i_fd, w_fd);
  free(pfd);
  free(sched->default_opt);
  free(sched);
  return 0;
}
