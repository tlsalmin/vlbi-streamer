#include <stdlib.h>
#include <stdio.h>
#include "../src/datatypes.h"
#include "common.h"
#include "../src/streamer.h"
#include "../src/config.h"
#include <string.h>

#define N_THREADS 128
#define NAMEDIVISION 2
#define N_FILES N_THREADS/NAMEDIVISION
#define PACKET_SIZE 1024
#define NUMBER_OF_PACKETS 4096
#define N_DRIVES 512

char ** filenames;
struct opt_s* dopt;
struct opt_s* opts;

void* testrun()
{
  return NULL;
}

int main()
{
  int i,err;
  struct opt_s * dopt = malloc(sizeof(struct opt_s));
  CHECK_ERR_NONNULL(dopt, "default opt malloc");
  clear_and_default(dopt,0);
  dopt->optbits &= ~LOCKER_CAPTURE;
  dopt->optbits |= CAPTURE_W_DUMMY;
  dopt->optbits &= ~LOCKER_REC;
  dopt->optbits |= REC_DUMMY;
  dopt->packet_size = PACKET_SIZE;
  dopt->buf_num_elems = NUMBER_OF_PACKETS;
  dopt->n_drives = N_DRIVES;
  dopt->filesize = PACKET_SIZE*NUMBER_OF_PACKETS;
  dopt->maxmem = 1;
  dopt->cumul = (long unsigned *)malloc(sizeof(long unsigned));
  *dopt->cumul = 0;
  dopt->total_packets = (long unsigned *)malloc(sizeof(long unsigned));
  *dopt->total_packets = 0;


  opts = malloc(sizeof(struct opt_s)*N_THREADS);
  CHECK_ERR_NONNULL(opts, "malloc opt");
  struct thread_data thread_data[N_THREADS];

  filenames = (char**)malloc(sizeof(char*)*N_FILES);
  CHECK_ERR_NONNULL(filenames, "malloc filenames");
  for(i=0;i<N_FILES;i++){
    filenames[i] = (char*) malloc(sizeof(char)*FILENAME_MAX);
    CHECK_ERR_NONNULL(filenames[i], "malloc filename");
    memset(filenames[i], 0, sizeof(char)*FILENAME_MAX);
    sprintf(filenames[i], "%s%d", "filename", i%10);
  }

  for(i=0;i<N_THREADS;i++)
  {
    thread_data[i].thread_id = i; 
    thread_data[i].status = THREAD_STATUS_NOT_STARTED; 
    thread_data[i].filename = filenames[i % N_FILES];
    thread_data[i].intid = i*N_THREADS;
    memcpy(&(opts[i]), dopt, sizeof(struct opt_s));
  }
  err = init_active_file_index();
  CHECK_ERR("active file index");
  
  err = init_branches(dopt);
  CHECK_ERR("Init branches");

  err = init_recp(dopt);
  CHECK_ERR("Init recpoints");

  err = init_rbufs(dopt);
  CHECK_ERR("init buffers");


  D("Closing membranch and diskbranch");
  close_rbufs(dopt, NULL);
  D("Membranch closed");
  close_recp(dopt,NULL);
  D("Membranch and diskbranch shut down");

  for(i=0;i<N_FILES;i++){
    free(filenames[i]);
  }

  close_active_file_index();

  free(filenames);
  free(opts);
  close_opts(dopt);
  return 0;
}
