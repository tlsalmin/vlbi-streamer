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
#include <sys/file.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>

#include "config.h"
#ifdef LOG_TO_FILE
#undef LOG_TO_FILE
#define LOG_TO_FILE 0
#endif
#include "logging.h"
#define STATEFILE LOCALSTATEDIR "/opt/vlbistreamer/schedule"

int fd,err;

void usage(char *bin)
{
  LOG("Usage: %s <Recording ID>\n", bin);
}
void exitfunc()
{
  err = flock(fd, LOCK_UN);
  if(err != 0)
    E("Error in unlocking schedule file");
  close(fd);
}
int main(int argc, char ** argv)
{
  char * recname;
  config_t cfg;
  if(argc != 2)
    usage(argv[0]);
  
  recname = argv[1];
  
  err = open(STATEFILE,O_RDWR);
  CHECK_ERR_LTZ("Open schedule file");
  fd = err;
  err = flock(fd, LOCK_SH);
  CHECK_ERR("Lock statefile");

  err = atexit(&exitfunc);

  config_init(&cfg);
  config_read_file(&cfg, STATEFILE);
  config_setting_t *root;

  root = config_root_setting(&cfg);
  err = config_setting_remove(root, recname);
  CHECK_CFG("Remove recording from schedule");
  err = config_write_file(&cfg, STATEFILE);
  CHECK_CFG("Write config file");
  config_destroy(&cfg);

  return 0;
}
