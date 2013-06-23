#ifndef SERVER_H
#define SERVER_H
#define CFGFILE SYSCONFDIR "/vlbistreamer.conf"
#define STATEFILE LOCALSTATEDIR "/opt/vlbistreamer/schedule"
#define SCHEDLOCKFILE LOCALSTATEDIR "/opt/vlbistreamer/vlbistreamer_schedlockfile"
#define LSOCKNAME LOCALSTATEDIR "/opt/vlbistreamer/local_socket"
#define LOGFILE	LOCALSTATEDIR "/log/vlbistreamer.log"
/* Not actually used heh.. */
#define MAX_SCHEDULED 512
#define SECS_TO_START_IN_ADVANCE 1
#define MAX_INOTIFY_EVENTS MAX_SCHEDULED
#define IDSTRING_LENGTH 24
#define CHAR_BUFF_SIZE ((sizeof(struct inotify_event)+FILENAME_MAX)*MAX_INOTIFY_EVENTS)

#define POLLINTERVAL 100
#define STATTIME 1000

void zero_sched(struct schedule *sched);
void zero_schedevnt(struct scheduled_event* ev);
int free_and_close(void *le);
int remove_from_cfgsched(struct scheduled_event *ev);
int start_event(struct scheduled_event *ev);
int start_scheduled(struct schedule *sched);
int sched_identify(void* opt, void *val1, void * val2, int iden_type);
int add_recording(config_setting_t* root, struct schedule* sched, int socketnumber);
void zerofound(struct schedule *sched);
int check_schedule(struct schedule *sched);
int check_finished(struct schedule* sched);
#endif
