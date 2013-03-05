#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "localsocket_service.h"
#define BUFFER_SIZE 1024

void* localsocket_service(void* arg)
{
  int err;
  int alreadyshutdown=0;
  char inputbuffer[BUFFER_SIZE];
  memset(inputbuffer,0,sizeof(char)*BUFFER_SIZE);
  struct opt_s* opt = (struct opt_s*)arg;

  while(opt->status == STATUS_RUNNING);
  {
    err = recv(opt->localsocket, (void*)inputbuffer, BUFFER_SIZE-1, 0);
    if(err < 0){
      E("Error in receiving from local socket");
      opt->status = STATUS_ERROR;
    }
    if(err == 0){
      LOG("local socket shut down");
      opt->status = STATUS_FINISHED;
      alreadyshutdown=1;
    }
    else
    {
      LOG("local socket says %s",, inputbuffer);
    }
    memset(localsocket, 0, sizeof(char)*BUFFER_SIZE);
  }

  if(alreadyshutdown == 0)
    shutdown(opt->localsocket, SHUT_RDWR);
  close(opt->localsocket);
  pthread_exit(NULL);
}
void ls_shutdown(void *arg)
{
  struct opt_s* opt = (struct opt_s*)arg;
  shutdown(opt->localsocket, SHUT_RDWR);
}
