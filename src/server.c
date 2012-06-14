#include <sys/inotify.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "config.h"
#include "streamer.h"
#define CFGFILE SYSCONFDIR "/vlbistreamer.conf"
#define STATEFILE LOCALSTATEDIR "/opt/vlbistreamer/schedule"
#define LOGFILE	LOCALSTATEDIR "/log/vlbistreamer.log"
#define MAX_SCHEDULED 512
#define MAX_RUNNING 4

struct scheduled_event{
  struct timespec event_time;
  char * name;
  struct opt_s * opt;
};

int main(int argc, char **argv)
{
  int err,is_running = 1;
  struct scheduled_event * scheduled[MAX_SCHEDULED];
  struct scheduled_event * running[MAX_RUNNING];
  struct opt_s * default_opt = malloc(sizeof(struct opt_s));

  /* First load defaults to opts, then check default config file	*/
  /* and lastly check command line arguments. This means config file	*/
  /* overrides defaults and command line arguments override config file	*/
  clear_and_default(default_opt);

  default_opt->cfgfile = CFGFILE;
  err = read_full_cfg(default_opt);
  CHECK_ERR("Load default cfg");

  parse_options(argc,argv,default_opt);

  int i_fd = inotify_init();
  CHECK_LTZ("Inotify init", i_fd);

  D("Starting watch on statefile %s",, STATEFILE);
  err = inotify_add_watch(i_fd, STATEFILE, IN_MODIFY);
  CHECK_ERR_LTZ("Add watch");

  /* Remove when ready */
  (void)scheduled;
  (void)running;

  while(is_running)
  {
    is_running = 0;
  }

  free(default_opt);
  return 0;
}
