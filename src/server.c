/*
 * server.c -- Main thread and schedule manager for vlbistreamer
 *
 * Written by Tomi Salminen (tlsalmin@gmail.com)
 * Copyright 2012 Metsähovi Radio Observatory, Aalto University.
 * All rights reserved
 * This file is part of vlbi-streamer.
 *
 * vlbi-streamer is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * vlbi-streamer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with vlbi-streamer.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */
#define _GNU_SOURCE
#include <sys/inotify.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <libconfig.h>
#include <string.h> /* MEMSET */
#include <poll.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/time.h>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>

#include "config.h"
#include "localsocket_service.h"
#include "streamer.h"
#include "confighelper.h"
#include "server.h"
#include "active_file_index.h"
//#define TERM_SIGNAL_HANDLING


static volatile int running = 0;
extern FILE *logfile;

struct schedule *sched;

void zero_sched(struct schedule *sched){
  memset(sched,0,sizeof(struct schedule));
  //sched->scheduled_head = NULL;
  //sched->running_head = NULL;
  /*
  sched->br.freelist=NULL;
  sched->br.busylist=NULL;
  sched->br.loadedlist=NULL;
  sched->default_opt = NULL;
  sched->n_scheduled = 0;
  sched->n_running =0;
  */
  sched->br.mutex_free = MUTEX_FREE;
}
void zero_schedevnt(struct scheduled_event* ev){
  ev->opt = NULL;
  //ev->next =NULL;
  //ev->father = NULL;
  //ev->child = NULL;
  ev->socketnumber = 0;
  ev->stats =NULL;
  ev->pt =0;
  ev->found=0;
}
int remove_from_cfgsched(struct scheduled_event *ev){
  int err;
  config_t cfg;
  config_init(&cfg);
  config_read_file(&cfg, STATEFILE);
  config_setting_t *root;

  root = config_root_setting(&cfg);
  err = config_setting_remove(root, ev->idstring);
  if(err == CONFIG_FALSE){
    LOG("Cant remove cfg as its already removed.\n");
    config_destroy(&cfg);
    return 0;
  }
  //CHECK_CFG("remove closed recording");
  err = config_write_file(&cfg, STATEFILE);
  CHECK_CFG("Wrote config");
  LOG("Updated config file and removed %s\n", ev->opt->filename);
  config_destroy(&cfg);
  return 0;
}
int free_and_close(void *le){
  struct scheduled_event * ev = (struct scheduled_event*) le;

  D("optstatus: %d",, ev->opt->status);
  int err;
  if(ev->opt->optbits & READMODE)
  {
    if(ev->opt->hostname != NULL)
      LOG("Sending to %s of ", ev->opt->hostname);
    else
      LOG("Sending of ");
  }
  else
    LOG("Receiving of ");

  switch(ev->opt->status)
  {
    case STATUS_FINISHED:
      LOG("%s finished OK\n",ev->opt->filename);
      break;
    case STATUS_ERROR:
      LOG("%s finished in ERROR\n",ev->opt->filename);
      break;
    case STATUS_STOPPED:
      LOG("%s has stopped\n", ev->opt->filename);
      break;
    case STATUS_RUNNING:
      LOG("%s Still running. cancelling\n",ev->opt->filename);
      D("Still running");
      if(ev->shutdown_thread != NULL){
	ev->shutdown_thread(ev->opt);
	D("Thread shut down");
      }
      break;
    case STATUS_CANCELLED:
      LOG("%s Cancelled\n", ev->opt->filename);
      if(ev->shutdown_thread != NULL){
	LOG("Forcefully shutting down\n");
	ev->shutdown_thread(ev->opt);
	D("Thread shut down");
      }
      break;
    default:
      LOG("%s in unknown mode\n",ev->opt->filename);
      if(ev->shutdown_thread != NULL){
	ev->shutdown_thread(ev->opt);
	LOG("Thread shut down\n");
      }
      //ev->opt->status = STATUS_CANCELLED;
  }
  /* Do this only if pthread started */
  if(ev->pt != 0){
    err = pthread_join(ev->pt, NULL);
    CHECK_ERR("join");
    err = close_streamer(ev->opt);
    CHECK_ERR("close stream");
  }
  if(ev->socketnumber == 0)
  {
    err = remove_from_cfgsched(ev);
    /* Not a critical error */
    if(err !=0)
      E("Removing cfg entry from sched");
    free(ev->idstring);
  }
  if(ev->opt->optbits & READMODE){
    err = disassociate(ev->opt->fi, FILESTATUS_SENDING);
    if(err != 0)
      E("Error in disassociate");
  }
  else{
    err = disassociate(ev->opt->fi, FILESTATUS_RECORDING);
    if(err != 0)
      E("Error in disassociate");
  }
  close_opts(ev->opt);
  free(ev);
  return 0;
}
int start_event(struct scheduled_event *ev)
{
  int err;
  struct opt_s *opt = ev->opt;

  if(!(ev->opt->optbits & CAPTURE_W_LOCALSOCKET))
  {
    err = init_cfg(ev->opt);
    if(err != 0){
      E("Error in cfg init");
      return -1;
    }
  }
  /* Special case when recording was stripped and buf_num_elems is different */
  if((opt->optbits & READMODE) && opt->offset_onwrite != 0){
    opt->buf_num_elems = opt->filesize / (opt->packet_size+opt->offset_onwrite);
    D("Packet size is %ld so num elems is %d since offset_onwrite was %d",, opt->packet_size, opt->buf_num_elems, opt->offset_onwrite);
  }
  else{
    opt->buf_num_elems = opt->filesize / opt->packet_size;
    D("Packet size is %ld so num elems is %d",, opt->packet_size, opt->buf_num_elems);
  }
  ev->opt->status = STATUS_RUNNING;
  if(ev->opt->optbits & VERBOSE){
    ev->stats = (struct stats*)malloc(sizeof(struct stats));
    init_stats(ev->stats);
  }
  if(ev->opt->optbits & WRITE_TO_SINGLE_FILE)
  {
    D("Write to single file set");
    ev->opt->writequeue = malloc(sizeof(pthread_mutex_t));
    CHECK_ERR_NONNULL_AUTO(ev->opt->writequeue);
    err = pthread_mutex_init(ev->opt->writequeue, NULL);
    CHECK_ERR("init writequeue");
    ev->opt->writequeue_signal = malloc(sizeof(pthread_cond_t));
    CHECK_ERR_NONNULL_AUTO(ev->opt->writequeue_signal);
    err = pthread_cond_init(ev->opt->writequeue_signal, NULL);
    CHECK_ERR("init writequeue signal");
  }
  err = pthread_create(&ev->pt, NULL, vlbistreamer, (void*)ev->opt); 
  CHECK_ERR("streamer thread create");
  /* TODO: check if packet size etc. match original config */
  return 0;
}
int start_scheduled(struct schedule *sched){
  int err=0;
  long tdif;
  TIMERTYPE time_now;
  GETTIME(time_now);
  struct scheduled_event * ev;
  //struct scheduled_event * parent = NULL;
  struct listed_entity* le;
  //for(ev = (struct scheduled_event*)sched->br->freelist->opt;ev != NULL;){
  //for(le = sched->br.freelist;le != NULL;le=le->child){
  le = sched->br.freelist;
  while(le != NULL){
    ev = le->entity;
    tdif = get_sec_diff(&time_now, &ev->opt->starting_time);
    if(ev->opt->starting_time.tv_sec == 0 || tdif <= SECS_TO_START_IN_ADVANCE){
      //struct scheduled_event * ev_temp = ev;
      //ev = ev->next;
      /* Removes old stuff. IF tv_sec is 0, should start immediately */
      if(ev->opt->starting_time.tv_sec != 0 && (!(ev->opt->optbits & READMODE) && (tdif < -((long)ev->opt->time)))){
	LOG("Removing clearly too old recording request %s, which should have started %ld seconds ago\n", ev->opt->filename, -tdif);
	LOG("Start time %lu\n", ev->opt->starting_time.tv_sec);
	//remove_recording(ev_temp,&(sched->scheduled_head));
	struct listed_entity* temp = le;
	le = le->child;
	remove_from_branch(&sched->br, temp, MUTEX_FREE);
	continue;
      }
      LOG("Starting event %s\n", ev->opt->filename);
      if(ev->opt->starting_time.tv_sec == 0){
	D("Setting start time as now for better timing");
	ev->opt->starting_time.tv_sec = time_now.tv_sec;
      }
      if(GETSECONDS(sched->lasttick) == 0)
	GETTIME(sched->lasttick);
      sched->n_scheduled--;
      //err = start_event(ev, sched);
      err = start_event(ev);
      if(err != 0){
	E("Something went wrong in recording %s start",, ev->opt->filename);
	//remove_recording(le,sched->br.freelist);
	remove_from_branch(&sched->br, le, MUTEX_FREE);
      }
      else{
	//change_sched_branch(&sched->scheduled_head, &sched->running_head, ev);
	mutex_free_change_branch(&sched->br.busylist, le);
	sched->n_running++;
      }
      /* Reset the search, since le has disappeared from freelist */
      le = sched->br.freelist;
      //set_running(sched, ev, parent);
    }
    else
      le = le->child;
  }
  return 0;
}
int sched_identify(void* opt, void *val1, void * val2, int iden_type){
  struct scheduled_event* ev = (struct scheduled_event*)opt;
  if(iden_type == CHECK_BY_IDSTRING){
    if(strcmp((char*)val1, ev->idstring) == 0)
      return 1;
    else
      return 0;

  }
  else if (iden_type == CHECK_BY_NOTFOUND){
    if(ev->found == 0)
      return 1;
    else
      return 0;
  }
  else if (iden_type == CHECK_BY_SOCKETNUM)
  {
    if(ev->socketnumber == *(int*)val1)
      return 1;
    else
      return 0;
  }
  else 
    return iden_from_opt(ev->opt, val1, val2, iden_type);
}
int add_recording(config_setting_t* root, struct schedule* sched, int socketinterface)
{
  D("Adding new schedevent");
  int err;
  if(root != NULL)
  {
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
      running = 0;


      config_destroy(&cfg);
      D("Shutdown finished");
      return 0;
    }
  }
  struct listed_entity * le = (struct listed_entity *)malloc(sizeof(struct listed_entity));
  struct scheduled_event * se = (struct scheduled_event*)malloc(sizeof(struct scheduled_event));
  zero_schedevnt(se);
  le->entity= se;
  le->child = NULL;
  le->father = NULL;
  le->identify = sched_identify;
  le->close = free_and_close;
  CHECK_ERR_NONNULL(se, "Malloc scheduled event");
  struct opt_s *opt = malloc(sizeof(struct opt_s));
  CHECK_ERR_NONNULL(opt, "Opt for event malloc");
  se->shutdown_thread = shutdown_thread;

  se->found=1;
  //se->next=NULL;
  se->opt = opt;
  /* Copy the default opt over our opt	*/
  clear_and_default(opt,0);
  /* membranch etc. should be copied here also */
  memcpy(opt,sched->default_opt, sizeof(struct opt_s));

  clear_pointers(opt);

  if(socketinterface != 0)
  {
    D("New event is a local socket interface");
    se->opt->optbits &= ~LOCKER_CAPTURE;
    se->opt->optbits |= CAPTURE_W_LOCALSOCKET;
    se->opt->optbits |= READMODE;
    se->opt->localsocket = socketinterface;
  }

  /*
     opt->cumul = (long unsigned *)malloc(sizeof(long unsigned));
   *opt->cumul = 0;
   */

  if(root != NULL)
    config_init(&(opt->cfg));

  /* Get the name of the recording	*/
  opt->filename = (char*)malloc(sizeof(char)*FILENAME_MAX);
  CHECK_ERR_NONNULL(opt->filename, "Filename for opt malloc");
  opt->disk2fileoutput = (char*)malloc(sizeof(char)*FILENAME_MAX);
  CHECK_ERR_NONNULL(opt->disk2fileoutput, "disk2fileoutput for opt malloc");
  if(!(se->opt->optbits & CAPTURE_W_LOCALSOCKET))
  {
    se->idstring = (char*)malloc(sizeof(char)*24);
    CHECK_ERR_NONNULL(se->idstring, "idstring malloc");
    //strcpy(opt->filename, config_setting_name(root));
    if(root != NULL)
    {
      strcpy(se->idstring, config_setting_name(root));
      LOG("Adding new request with id string: %s\n", se->idstring);
      /* Get the rest of the opts		*/
      err = set_from_root(opt, root, 0,0);
      if(err != 0){
	E("Broken schedule config. Not scheduling %s",, se->idstring);
	free_and_close(se);
	return 0;
      }
    }
    else
    {
      E("config root is null, eventhough reading recording metadata from file");
      return -1;
    }
  }



  /* Wont need disk2file output if CAPTURE_W_DISK2FILE set */
  if (!(opt->optbits & CAPTURE_W_DISK2FILE)){
    free(opt->disk2fileoutput);
    opt->disk2fileoutput = NULL;
  }

  /* Null here, set in initializer. */
  opt->get_stats = NULL;

  /* We might need to calc buf_num_elems again */


  if(!(se->opt->optbits & CAPTURE_W_LOCALSOCKET))
  {
    LOG("New request is for session: %s\n", opt->filename);
    D("Opts checked, port is %d",, opt->port);
  }

  add_to_entlist(&(sched->br), le);
  if(se->opt->optbits & CAPTURE_W_LOCALSOCKET)
  {
    err = start_event(se);
    if(err != 0){
      E("Something went wrong in recording %s start",, se->opt->filename);
      //remove_recording(le,sched->br.freelist);
      remove_from_branch(&sched->br, le, MUTEX_FREE);
    }
    else{
      //change_sched_branch(&sched->scheduled_head, &sched->running_head, ev);
      mutex_free_change_branch(&sched->br.busylist, le);
      sched->n_running++;
    }
  }
  else
  {
    sched->n_scheduled++;
  }
  D("Schedevent added");
  return 0;
}
void zerofound(struct schedule *sched){
  struct listed_entity *le;
  struct scheduled_event * temp;
  for(le = sched->br.freelist; le != NULL; le=le->child){
    temp = (struct scheduled_event*)le->entity;
    temp->found = 0;
  }
  for(le = sched->br.busylist; le != NULL; le=le->child){
    temp = (struct scheduled_event*)le->entity;
    temp->found = 0;
  }
}
int check_schedule(struct schedule *sched){
  int i=0,err;
  struct scheduled_event * temp = NULL;
  struct listed_entity * le = NULL;
  config_t cfg;
  config_init(&cfg);
  config_read_file(&cfg, STATEFILE);
  config_setting_t *root, *setting;

  root = config_root_setting(&cfg);

  D("Checking schedule");
  /* Go through all the scheduled recordings	 			*/
  /* If theres a new one: Add it to the scheduled			*/
  /* If one that was there is now missing, remove it from schedule	*/
  /* else just set the events found to 1				*/
  for(setting=config_setting_get_elem(root,i);setting != NULL;setting=config_setting_get_elem(root,++i)){
    le = get_from_all(&(sched->br), config_setting_name(setting), NULL, CHECK_BY_IDSTRING, MUTEX_FREE, NULL);
    //temp = get_event_by_name(config_setting_name(setting), sched);
    /* New scheduled recording! 					*/
    if(le == NULL){
      D("New schedule event found");
      err = add_recording(setting, sched,0);
      CHECK_ERR("Add recording");
    }
    else{
      D("Found ye olde");
      temp = (struct scheduled_event *)le->entity;
      temp->found = 1;
    }
  }
  while((le = get_from_all(&sched->br, NULL, NULL, CHECK_BY_NOTFOUND, 1, NULL)) != NULL){
    temp = (struct scheduled_event*)le->entity;
    LOG("Recording %s removed from schedule\n", temp->opt->filename);
    if(temp->opt->status == STATUS_RUNNING){
      sched->n_running--;
      temp->opt->status = STATUS_CANCELLED;
    }
    else
      sched->n_scheduled--;

    //err =  remove_recording(temp, &(sched->scheduled_head));
    if(temp->stats != NULL)
      free(temp->stats);
    remove_from_branch(&sched->br, le, MUTEX_FREE);
    //CHECK_ERR("Remove recording");
  }
  zerofound(sched);
  config_destroy(&cfg);
  D("Done checking schedule");

  return 0;
}
int handle_localsocket(int local_sock,struct schedule* sched)
{
  int err, clisocket, retval=0;
  struct sockaddr_in * cliaddr = malloc(sizeof(struct sockaddr_in));
  socklen_t slen = sizeof(struct sockaddr_in);
  CHECK_ERR_NONNULL(cliaddr, "sockaddr malloc");
  memset(cliaddr, 0,sizeof(struct sockaddr_in));

  clisocket = accept4(local_sock, (struct sockaddr*)cliaddr, &slen, SOCK_NONBLOCK);
  if(!(clisocket > 0)){
    E("Error in accept");
    free(cliaddr);
    return -1;
  }
  err = add_recording(NULL, sched, clisocket);
  if(err != 0){
    E("Error in add recording");
    retval = -1;
  }
  free(cliaddr);
  return retval;
}

int check_default_options_for_nonlogicals(struct opt_s* opt)
{
  if(opt->optbits & WRITE_TO_SINGLE_FILE)
  {
    if(WRITEND_USES_DIRECTIO(opt)){
      E("Cant run with WRITE_TO_SINGLE_FILE without write backend as either WRITEV or SPLICER");
      return -1;
    }
  }
  return 0;
}

int check_finished(struct schedule* sched){
  struct scheduled_event *ev;
  struct listed_entity *le;
  //int err;
  for(le = sched->br.busylist;le!=NULL;){
    struct listed_entity *letemp = le->child;
    ev = (struct scheduled_event*)le->entity;
    if(ev->opt->status & (STATUS_FINISHED | STATUS_ERROR |STATUS_CANCELLED | STATUS_STOPPED)){
      //err = close_recording(sched, ev);
      if(ev->opt->optbits & VERBOSE)
	free(ev->stats);
      //err = remove_recording(ev, &(sched->running_head));
      remove_from_branch(&(sched->br), le, MUTEX_FREE);
      //CHECK_ERR("close recording");
      sched->n_running--;
      /* Reset our time, so it gets formatted when we next time start a recording	*/
      if(sched->n_running == 0)
	ZEROTIME(sched->lasttick);
    }
    le = letemp;
  }
  return 0;
}
#ifdef TERM_SIGNAL_HANDLING
//TODO: Fix these. Need to add some pthread_cancel-stuff to kill 
//malfunctioning threads properly
static void hdl (int sig, siginfo_t *siginfo, void *context)
{
  (void)sig;
  (void)context;
  LOG("Sending PID: %ld, UID: %ld\n",
      (long)siginfo->si_pid, (long)siginfo->si_uid);
  LOG("Signal received");
  running = 0;
}
#endif
int main(int argc, char **argv)
{
  int err,i_fd,w_fd,counter;
#if(LOG_TO_FILE)
  fprintf(stdout, "Logging to %s", LOGFILE);
  logfile = fopen(LOGFILE, "a+");
  if(logfile == NULL){
    fprintf(stdout, "Couldn't open logfile %s for writing", LOGFILE);
    exit(-1);
  }
#endif
  struct sockaddr_un lsock_name;
  int local_sock;
#if(PPRIORITY)
  LOG("Priority before sleep in getprio %d\n", getpriority(PRIO_PROCESS, syscall(SYS_gettid)));
  sleep(1);
  LOG("Priority after sleep in getprio %d\n", getpriority(PRIO_PROCESS, syscall(SYS_gettid)));
#endif

  struct stats* tempstats = NULL;//, stats_temp;
  TIMERTYPE *temptime = NULL;

  sched = malloc(sizeof(struct schedule));
  CHECK_ERR_NONNULL(sched, "Sched malloc");

#ifdef TERM_SIGNAL_HANDLING
  /* Copied from http://www.linuxprogrammingblog.com/all-about-linux-signals?page=show */
  struct sigaction act;

  memset (&act, '\0', sizeof(act));

  // Use the sa_sigaction field because the handles has two additional parameters 
  act.sa_sigaction = &hdl;

  // The SA_SIGINFO flag tells sigaction() to use the sa_sigaction field, not sa_handler. 
  act.sa_flags = SA_SIGINFO;

  if (sigaction(SIGTERM, &act, NULL) < 0) {
    perror ("sigaction");
    return 1;
  }
#endif

  zero_sched(sched);
  /* Set the branch as mutex free */
  sched->br.mutex_free = 1;
  struct stats* stats_full = (struct stats*)malloc(sizeof(struct stats));
  CHECK_ERR_NONNULL(stats_full, "stats malloc");

  sched->default_opt = malloc(sizeof(struct opt_s));
  CHECK_ERR_NONNULL(sched->default_opt, "Default opt malloc");
  char *ibuff = (char*)malloc(sizeof(char)*CHAR_BUFF_SIZE);
  CHECK_ERR_NONNULL(ibuff, "ibuff malloc");
  memset(ibuff, 0,sizeof(char)*CHAR_BUFF_SIZE);

  err = init_active_file_index();
  CHECK_ERR("active file index");

  /* First load defaults to opts, then check default config file	*/
  /* and lastly check command line arguments. This means config file	*/
  /* overrides defaults and command line arguments override config file	*/
  clear_and_default(sched->default_opt,1);

  err = parse_options(argc,argv,sched->default_opt);
  CHECK_ERR("parse options");

  /* Check for correct options */
  err = check_default_options_for_nonlogicals(sched->default_opt);
  CHECK_ERR("Check for nonlogicals in default opt");

  memset(&lsock_name, 0, SUN_LEN(&lsock_name));
  lsock_name.sun_family = AF_UNIX; 
  sprintf(lsock_name.sun_path, "%s", LSOCKNAME);

  local_sock = socket(PF_UNIX, SOCK_STREAM, 0);
  if(local_sock < 0){
    E("Error in creating local socket. vlbistreamer not listening");
  }
  else
  {
    unlink(lsock_name.sun_path);
    size_t size_lsock = SUN_LEN(&lsock_name);
    err = bind(local_sock, (struct sockaddr *) &lsock_name, size_lsock);
    if(err != 0)
    {
      E("Error in binding local domain socket");
    }
    else
    {
      err = listen(local_sock, 12);
      if(err != 0){
	E("Error in listen");
      }
      else
      {
	D("Local socket listening as %s",, lsock_name.sun_path);
      }
    }
  }

  LOG("Running in daemon mode\n");

  //stuff stolen from http://darkeside.blogspot.fi/2007/12/linux-inotify-example.html
  if(sched->default_opt->cfgfile == NULL){
    sched->default_opt->cfgfile = (char*)malloc(sizeof(char)*FILENAME_MAX);
    CHECK_ERR_NONNULL(sched->default_opt->cfgfile, "cfgfile malloc");
    sprintf(sched->default_opt->cfgfile, "%s", CFGFILE);
    //sched->default_opt->cfgfile = CFGFILE;
    err = read_full_cfg(sched->default_opt);
    CHECK_ERR("Load default cfg");
    LOG("Read config from %s\n", sched->default_opt->cfgfile);

    err = parse_options(argc,argv,sched->default_opt);
    CHECK_ERR("parse options");
  }

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
  struct pollfd * pfd = (struct pollfd*)malloc(sizeof(struct pollfd)*2);
  CHECK_ERR_NONNULL(pfd, "pollfd malloc");
  memset(pfd, 0,sizeof(struct pollfd)*2);

  (pfd+1)->fd = local_sock;
  (pfd+1)->events = POLLIN|POLLERR;

  pfd->fd = i_fd;
  pfd->events = POLLIN|POLLERR;

  //sched->running = 1;
  counter = 0;
  running = 1;

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
  while(running == 1)
  {
    err = start_scheduled(sched);
    //CHECK_ERR("Start scheduled");
    if(err != 0)
      running = 0;
    /* Check for changes 						*/
    err = poll(pfd, 2, POLLINTERVAL);
    if(err < 0){
      perror("Poll");
      running = 0;
    }
    else if(err >0)
    {
      if(pfd->revents & POLLIN)
      {
	err = read(i_fd, ibuff, CHAR_BUFF_SIZE);
	CHECK_ERR_LTZ("Read schedule");
	D("Noticed change in schedule file");
	/* There's really just one sort of event. Adding or removing	*/
	/* a scheduled recording					*/
	err = check_schedule(sched);
	if(err != 0)
	  running = 0;
      }
      else if (pfd->revents & POLLERR)
      {
	E("Error in polling for schedule file");
	running = 0;
      }
      if ((pfd+1)->revents & POLLERR)
      {
	E("Error in polling for local socket");
	//running=0;
      }
      if((pfd+1)->revents & POLLIN)
      {
	D("Local socket shows activity");
	err = handle_localsocket(local_sock, sched);
	if(err != 0)
	  E("Error in handling local socket");
      }
      //CHECK_ERR("Checked schedule");
    }
    //Else timeout expired

    counter += POLLINTERVAL;
    err = check_finished(sched);
    if (err != 0)
      running = 0;
    //CHECK_ERR("check finished");
    if(sched->default_opt->optbits & VERBOSE && sched->n_running > 0 && (counter % STATTIME ==0)){
      counter =0 ;
      err = print_midstats(sched, stats_full);
      CHECK_ERR("print stats");
    }
#if(LOG_TO_FILE)
    fflush(logfile);
#else
    fflush(stdout);
#endif
    if(check_if_alive(sched->default_opt->membranch) != 0){
      E("Memtree dead! Exiting!");
      running = 0;
    }
  }

  struct listed_entity * temp = sched->br.busylist;
  struct scheduled_event *ev;
  while(temp != NULL){
    struct listed_entity* temp2 = temp;
    temp = temp->child;
    ev = (struct scheduled_event*)temp2->entity;
    //if(ev->opt->optbits & VERBOSE)
    if(ev->stats != NULL)
      free(ev->stats);
    remove_from_branch(&sched->br,temp2, MUTEX_FREE);
    //ev = (struct scheduled_event*)temp->entity;
    //ev->shutdown_thread(ev->opt);
    //err = free_and_close(ev);
    /*
       if(ev->pt != 0){
       D("Joining thread");
       pthread_join(ev->pt, NULL);
       }
       */
    //temp = temp->next;
    //temp = temp->child;
  }
  D("Threads shut down");

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

  unlink(lsock_name.sun_path);
  close(local_sock);

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

  err = close_active_file_index();
  CHECK_ERR("Close active file index");

#if(LOG_TO_FILE)
  fclose(logfile);
#endif
  return 0;
}
