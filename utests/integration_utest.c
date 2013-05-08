#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "../src/datatypes.h"
#include "../src/streamer.h"
#include "../src/config.h"
#include "../src/active_file_index.h"
#include "../src/udp_stream.h"
#include "common.h"

#define N_THREADS 16
#define NAMEDIVISION 2
#define N_FILES N_THREADS/NAMEDIVISION
#define N_FILES_PER_BOM 60
#define PACKET_SIZE 1024
#define NUMBER_OF_PACKETS 4096
#define N_DRIVES 512
#define RUNTIME 10

#define CHECK_BRANCHES do{assert(dopt->membranch->busylist == NULL && dopt->membranch->loadedlist == NULL && dopt->diskbranch->busylist == NULL && dopt->diskbranch->loadedlist == NULL);}while(0)

#define RUN_SEND
#define RUN_SINGLE_SEND
#define RUN_SINGLE_RECEIVE
#define RUN_RECEIVE
#define RUN_SINGLE_SEND_AND_RECEIVE
#define RUN_FSINGLE_SEND_AND_RECEIVE
#define RUN_SEND_AND_RECEIVE

char ** filenames;
struct opt_s* dopt;
struct opt_s* opts;
struct scheduled_event* events;
struct stats * stats;
int prep_dummy_file_index(struct opt_s *opt)
{
  int j;
  if((opt->fi = get_fileindex(opt->filename, 1)) != NULL)
  {
    D("filename %s Already exists, so just going along with this..",, opt->filename);
    opt->cumul_found = get_n_files(opt->fi);
    return 0;
  }
  else
    D("filename %s Not found in index. Creating new",, opt->filename);
  opt->fi = add_fileindex(opt->filename, N_FILES_PER_BOM, FILESTATUS_SENDING, 0);
  CHECK_ERR_NONNULL(opt->fi, "start file index");
  FI_WRITELOCK(opt->fi);
  if(!((opt->fi->status) & FILESTATUS_RECORDING))
    opt->fi->n_packets = N_FILES_PER_BOM*NUMBER_OF_PACKETS;
  //opt->fi->n_packets = N_FILES_PER_BOM*NUMBER_OF_PACKETS;
  struct fileholder * fh = opt->fi->files;

  for(j=0;(unsigned)j<=opt->fi->n_files;j++){
    fh->diskid = (rand() % (N_DRIVES-1));
    fh->status = FH_ONDISK;
    fh++;
  }
  FIUNLOCK(opt->fi);

  opt->cumul_found = N_FILES_PER_BOM;

  return 0;
}

int start_thread(struct scheduled_event * ev)
{
  int err;

  if(!(ev->opt->optbits & READMODE)){
    TIMERTYPE now;
    GETTIME(now);
    ev->opt->fi = add_fileindex(ev->opt->filename, 0, FILESTATUS_RECORDING, ev->opt->packet_size);
    ev->opt->time = RUNTIME;
    GETSECONDS(ev->opt->starting_time) = GETSECONDS(now);
    CHECK_ERR_NONNULL(ev->opt->fi, "Add fileindex");
  }
  else{
    D("num elems wtf %d",, ev->opt->buf_num_elems);
    err = prep_dummy_file_index(ev->opt);
    CHECK_ERR("Prep dummies");
    ev->opt->hostname = NULL;
    D("num elems wtf %d",, ev->opt->buf_num_elems);
  }
  err = pthread_create(&ev->pt, NULL, vlbistreamer, (void*)ev->opt); 
  CHECK_ERR("streamer thread create");
  /* TODO: check if packet size etc. match original config */
  return 0;
}

int close_thread(struct scheduled_event *ev)
{
  int err;
  if(ev->opt->optbits & READMODE)
    assert(!(get_status(ev->opt->fi) & FILESTATUS_RECORDING));

  err = pthread_join(ev->pt,NULL);
  CHECK_ERR("pthread join");
  if(ev->opt->optbits & READMODE){
    D("Pthread sender joining on %s",, ev->opt->filename);
    err = disassociate(ev->opt->fi, FILESTATUS_SENDING);
    if(err != 0){
      E("Error in disassociate");
      return -1;
    }
  }
  else{
    D("Pthread recorder joining on %s",, ev->opt->filename);
    disassociate(ev->opt->fi, FILESTATUS_RECORDING);
    if(err != 0){
      E("Error in disassociate");
      return -1;
    }
  }
  if(ev->opt->status != STATUS_FINISHED){
    E("Thread didnt finish nicely");
    return -1;
  }
  get_udp_stats(ev->opt->streamer_ent->opt, ev->stats);
  print_stats(ev->stats, ev->opt);
  return 0;
}
int format_threads(struct opt_s* original, struct opt_s* copies)
{
  int i;
  for(i=0;i<N_THREADS;i++)
  {
    if(copies[i].cumul != NULL)
      free(copies[i].cumul);
    memcpy(&(copies[i]), original,sizeof(struct opt_s));
    clear_pointers(&(copies[i]));
    copies[i].cumul = (long unsigned *)malloc(sizeof(long unsigned));
    *(copies[i].cumul) = 0;
    events[i].opt = &(copies[i]);
    events[i].stats = &(stats[i]);
    /* This will give the same filename to each pair */
    D("Giving %d filename %s",, i, filenames[i/2]);
    copies[i].filename = filenames[i/2];
    if((i % 2) == 1){
      copies[i].optbits |= READMODE;
      copies[i].wait_nanoseconds = 5;
    }
    else
      copies[i].time = RUNTIME;
  }
  return 0;
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
  dopt->status = STATUS_NOT_STARTED;
  dopt->packet_size = PACKET_SIZE;
  dopt->buf_num_elems = NUMBER_OF_PACKETS;
  dopt->n_drives = N_DRIVES;
  dopt->hostname = NULL;
  dopt->filesize = PACKET_SIZE*NUMBER_OF_PACKETS;
  dopt->maxmem = 1;
  dopt->cumul = NULL;

  events = (struct scheduled_event*)malloc(sizeof(struct scheduled_event)*N_THREADS);
  stats = (struct stats*)malloc(sizeof(struct stats)*N_THREADS);
  memset(stats, 0, sizeof(struct stats)*N_THREADS);

  opts = malloc(sizeof(struct opt_s)*N_THREADS);
  CHECK_ERR_NONNULL(opts, "malloc opt");
  //struct thread_data thread_data[N_THREADS];
  memset(opts, 0,sizeof(struct opt_s)*N_THREADS);

  filenames = (char**)malloc(sizeof(char*)*N_FILES);
  CHECK_ERR_NONNULL(filenames, "malloc filenames");
  for(i=0;i<N_FILES;i++){
    filenames[i] = (char*) malloc(sizeof(char)*FILENAME_MAX);
    CHECK_ERR_NONNULL(filenames[i], "malloc filename");
    memset(filenames[i], 0, sizeof(char)*FILENAME_MAX);
    sprintf(filenames[i], "%s%d", "filename", i);
  }

  TEST_START(init_resources);
  err = init_active_file_index();
  CHECK_ERR("active file index");

  err = init_branches(dopt);
  CHECK_ERR("Init branches");

  err = init_recp(dopt);
  CHECK_ERR("Init recpoints");

  err = init_rbufs(dopt);
  CHECK_ERR("init buffers");

  err =  format_threads(dopt,opts);
  CHECK_ERR("Init threads");


  CHECK_BRANCHES;
  TEST_END(init_resources);

#ifdef RUN_SINGLE_RECEIVE
  TEST_START(only_receive_one);

  opts[0].time= RUNTIME;
  err = start_thread(&(events[0]));
  CHECK_ERR("start event");
  err = close_thread(&(events[0]));
  CHECK_ERR("Close event");
  if(stats[0].total_bytes > 0)
  {
    D("Captured %ld bytes",, stats[0].total_bytes);
  }
  else{
    E("No bytes captured!");
    return -1;
  }
  memset(&(stats[0]), 0, sizeof(struct stats));


  CHECK_BRANCHES;
  TEST_END(only_receive_one);
#endif

  format_threads(dopt,opts);
  memset(stats, 0, sizeof(struct stats));

#ifdef RUN_RECEIVE
  TEST_START(only_receive);

  for(i=0;i<N_THREADS;i+=2)
  {
    opts[i].time = RUNTIME;
    err = start_thread(&(events[i]));
    CHECK_ERR("Start thread");
  }
  for(i=0;i<N_THREADS;i+=2)
  {
    err = close_thread(&(events[i]));
    CHECK_ERR("Close thread");
    if(stats[i].total_bytes > 0)
    {
      D("Captured %ld bytes",, stats[i].total_bytes);
    }
    else{
      E("No bytes captured on id %d!",, i);
      return -1;
    }
  }
  CHECK_ERR("Whole receive test");

  CHECK_BRANCHES;
  TEST_END(only_receive);
#endif
  format_threads(dopt,opts);
  memset(stats, 0, sizeof(struct stats)*N_THREADS);
#ifdef RUN_SINGLE_SEND
  TEST_START(only_send_one);

  //opts[1].time= 10;
  err = start_thread(&(events[1]));
  CHECK_ERR("start thread");
  err = close_thread(&(events[1]));
  CHECK_ERR("Close thread");
  if (stats[1].total_bytes == PACKET_SIZE*NUMBER_OF_PACKETS*N_FILES_PER_BOM)
  {
    D("sent %ld bytes correctly",, stats[1].total_bytes);
  }
  else{
    E("Not enough bytes sent. Only %ld!",, stats[1].total_bytes);
    return -1;
  }

  CHECK_BRANCHES;
  TEST_END(only_send_one);
#endif // RUN_SINGLE_SEND

  format_threads(dopt,opts);
  memset(stats, 0, sizeof(struct stats)*N_THREADS);
#ifdef RUN_SEND
  TEST_START(only_send);
  for(i=1;i<N_THREADS;i+=2)
  {
    //opts[i].time = 10;
    err = start_thread(&(events[i]));
    CHECK_ERR("Start thread");
  }
  for(i=1;i<N_THREADS;i+=2)
  {
    err = close_thread(&(events[i]));
    CHECK_ERR("Close thread");
    if (stats[i].total_bytes == PACKET_SIZE*NUMBER_OF_PACKETS*N_FILES_PER_BOM)
    {
      D("sent %ld bytes correctly",, stats[i].total_bytes);
    }
    else{
      E("Not enough bytes sent. Only %ld for id %i!",, stats[i].total_bytes,i);
      return -1;
    }
  }
  CHECK_ERR("Whole only send test");
  CHECK_BRANCHES;
  TEST_END(only_send);
#endif
  format_threads(dopt,opts);
  memset(stats, 0, sizeof(struct stats)*N_THREADS);
#ifdef RUN_SINGLE_SEND_AND_RECEIVE
  TEST_START(send_and_receive_one);

  err = start_thread(&(events[0]));
  CHECK_ERR("Start receive");
  sleep(1);
  err = start_thread(&(events[1]));
  CHECK_ERR("Start send");
  err = close_thread(&(events[0]));
  CHECK_ERR("Close receiving thread");
  err = close_thread(&(events[1]));
  CHECK_ERR("Close sending thread");
  D("Closed a pair. Checking bytes");
  if (stats[0].total_bytes == stats[1].total_bytes)
  {
    D("sent %ld bytes correctly",, stats[0].total_bytes);
  }
  else{
    E("Not enough bytes sent. Only %ld sent when %ld received!",, stats[1].total_bytes, stats[0].total_bytes);
    return -1;
  }
  CHECK_BRANCHES;
  TEST_END(send_and_receive_one);
#endif
  format_threads(dopt,opts);
  memset(stats, 0, sizeof(struct stats)*N_THREADS);
#ifdef RUN_FSINGLE_SEND_AND_RECEIVE
  TEST_START(send_faster_and_receive_one);
  opts[1].wait_nanoseconds = 0;

  err = start_thread(&(events[0]));
  CHECK_ERR("Start receive");
  sleep(1);
  err = start_thread(&(events[1]));
  CHECK_ERR("Start send");
  err = close_thread(&(events[0]));
  CHECK_ERR("Close receiving thread");
  err = close_thread(&(events[1]));
  CHECK_ERR("Close sending thread");
  D("Closed a pair. Checking bytes");
  if (stats[0].total_bytes == stats[1].total_bytes)
  {
    D("sent %ld bytes correctly",, stats[0].total_bytes);
  }
  else{
    E("Not enough bytes sent. Only %ld sent when %ld received!",, stats[1].total_bytes, stats[0].total_bytes);
    return -1;
  }
  CHECK_BRANCHES;
  TEST_END(send_faster_and_receive_one);
#endif

  format_threads(dopt,opts);
  memset(stats, 0, sizeof(struct stats)*N_THREADS);
#ifdef RUN_SEND_AND_RECEIVE
  TEST_START(send_and_receive);
  for(i=0;i<N_THREADS;i+=2)
  {
    //opts[i].time = 10;
    err = start_thread(&(events[i]));
    CHECK_ERR("Start thread");
  }
  sleep(2);
  for(i=1;i<N_THREADS;i+=2)
  {
    //opts[i].time = 10;
    err = start_thread(&(events[i]));
    CHECK_ERR("Start thread");
  }
  int retval = 0;
  for(i=0;i<N_THREADS;i++)
  {
    if(i % 2 == 0)
    {
      D("Waiting for %s recording to stop",, events[i].opt->filename);
      err = close_thread(&(events[i]));
      CHECK_ERR("Close receiver");
    }
    else
    {
      D("Waiting for %s sending to stop",, events[i].opt->filename);
      err = close_thread(&(events[i]));
      CHECK_ERR("Close thread");
      D("Closed a pair. Checking bytes");
      if (stats[i].total_bytes == stats[i-1].total_bytes)
      {
	D("sent %ld bytes correctly",, stats[i].total_bytes);
      }
      else{
	E("Not enough bytes sent. Only %ld sent when %ld received on %s!",, stats[i].total_bytes, stats[i-1].total_bytes, events[i].opt->filename);
	retval--;
      }
    }
  }
  if(retval != 0){
    E("Atleast one thread didn't send & receive the same. Errors: %d",, retval);
    return -1;
  }
  CHECK_BRANCHES;
  TEST_END(send_and_receive);
#endif

  TEST_START(close_resources);
  D("Closing membranch and diskbranch");
  err = close_rbufs(dopt, NULL);
  CHECK_ERR("close rbufs");
  close_recp(dopt,NULL);
  D("Membranch and diskbranch shut down");

  for(i=0;i<N_FILES;i++){
    free(filenames[i]);
  }

  err = close_active_file_index();
  CHECK_ERR("Close active file index");

  for(i=0;i<N_THREADS;i++)
  {
    free(opts[i].cumul);
  }

  free(filenames);
  free(opts);
  free(stats);
  close_opts(dopt);
  TEST_END(close_resources);
  return 0;
}
