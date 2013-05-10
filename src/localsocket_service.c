#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "localsocket_service.h"
#define BUFFER_SIZE 1024

extern FILE* logfile;

void* localsocket_service(void* opts)
{
  int err=0;
  int alreadyshutdown=0;
  char inputbuffer[BUFFER_SIZE];
  memset(inputbuffer,0,sizeof(char)*BUFFER_SIZE);
  struct opt_s* opt = (struct opt_s*)opts;

  LOG("Local socket service started\n");
  while(opt->status == STATUS_RUNNING)
  {
    err = recv(opt->localsocket, (void*)inputbuffer, BUFFER_SIZE-1, 0);
    if(err < 0){
      E("Error in receiving from local socket");
      opt->status = STATUS_ERROR;
    }
    else if(err == 0){
      LOG("local socket shutdown\n");
      opt->status = STATUS_FINISHED;
      alreadyshutdown=1;
    }
    else
    {
      D("local socket says %s",, inputbuffer);
      memset(inputbuffer, 0, sizeof(char)*BUFFER_SIZE);
    }
  }
  //opt->status = STATUS_FINISHED;
  LOG("Local listening socket shuttind down\n");

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
