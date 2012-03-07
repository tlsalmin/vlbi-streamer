#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "streamer.h"
#include "fanout.h"
#include "udp_stream.h"
#include "aioringbuf.h"
#define CAPTURE_W_FANOUT 0
#define CAPTURE_W_UDPSTREAM 1
#define CAPTURE_W_TODO 2
#define TUNE_AFFINITY
//How many gbit/s. Used for fallocate

#define CORES 6
extern char *optarg;
extern int optind, optopt;


static struct opt_s opt;

/*
 * Stuff stolen from lindis sendfileudp
 */
static void usage(char *binary){
  fprintf(stderr, 
      "usage: %s [OPTION]... name time\n"
      "-i INTERFACE	Which interface to capture from\n"
      "-t {fanout|udpstream|TODO	Capture type(fanout only one available atm)\n"
      "-a {lb|hash}	Fanout type\n"
      "-n NUM	        Number of threads\n"
      "-s SOCKET	Socket number\n"
      ,binary);
}
void init_stat(struct stats *stats){
  stats->total_bytes = 0;
  stats->incomplete = 0;
  stats->total_written = 0;
  stats->total_packets = 0;
  stats->dropped = 0;
}
void print_stats(struct stats *stats, struct opt_s * opts){
  fprintf(stdout, "Stats for %s \n"
      "Packets: %lu\n"
      "Bytes: %lu\n"
      "Dropped: %lu\n"
      "Incomplete: %lu\n"
      "Written: %lu\n"
      "Time: %d\n"
      "Net receive Speed: %luMB/s\n"
      "HD write Speed: %luMB/s\n"
      ,opts->filename, stats->total_packets, stats->total_bytes, stats->dropped, stats->incomplete, stats->total_written,opts->time, (stats->total_bytes*8)/(1024*1024*opts->time), (stats->total_written*8)/(1024*1024*opts->time) );
}
int init_recpoint(struct rec_point *rp, struct opt_s *opt){
  struct stat statinfo;
  int err =0;
  /*
  if(opt->rec_type == WRITER_AIOW_RBUF)
    int f_flags = O_WRONLY|O_DIRECT|O_NOATIME|O_NONBLOCK;
  else
    int f_flags = O_WRONLY|O_NOATIME|O_NONBLOCK;
    */
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "STREAMER: Initializing write point\n");
#endif
  //Check if file exists
  err = stat(rp->filename, &statinfo);
  if (err < 0) 
    if (errno == ENOENT){
      opt->f_flags |= O_CREAT;
      err = 0;
      //fprintf(stdout, "file doesn't exist\");
    }


  //This will overwrite existing file.TODO: Check what is the desired default behaviour 
  rp->fd = open(rp->filename, opt->f_flags, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
  if(rp->fd == -1){
    fprintf(stderr,"Error %s on %s\n",strerror(errno), rp->filename);
    return -1;
  }
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "STREAMER: File opened\n");
#endif
  //TODO: Set offset accordingly if file already exists. Not sure if
  //needed, since data consistency would take a hit anyway
  rp->offset = 0;
  //RATE = 10 Gb => RATE = 10*1024*1024*1024/8 bytes/s. Handled on n_threads
  //for s seconds.
  loff_t prealloc_bytes = (RATE*opt->time*1024)/(opt->n_threads*8);
  //Split kb/gb stuff to avoid overflow warning
  prealloc_bytes = prealloc_bytes*1024*1024;
  //set flag FALLOC_FL_KEEP_SIZE to precheck drive for errors
  err = fallocate(rp->fd, 0,0, prealloc_bytes);
  if(err == -1){
    fprintf(stderr, "Fallocate failed on %s", rp->filename);
    return err;
  }
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "STREAMER: File preallocated\n");
#endif
  //Uses AIOWRITER atm. TODO: Make really generic, so you can change the backends
  //aiow_init((void*)&(spec_ops->rbuf), (void*)spec_ops->rp);
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "STREAMER: AIOW initialized\n");
#endif
  return err;
}
static void parse_options(int argc, char **argv){
  int ret,i;

  memset(&opt, 0, sizeof(struct opt_s));
  opt.filename = NULL;
  opt.device_name = NULL;
  opt.capture_type = CAPTURE_W_FANOUT;
  opt.fanout_type = PACKET_FANOUT_LB;
  opt.root_pid = getpid();
  opt.port = 2222;
  opt.n_threads = 1;
  //TODO: Add option for choosing backend
  opt.rec_type = WRITER_AIOW_RBUF;
  opt.socket = 0;
  for(;;){
    ret = getopt(argc, argv, "i:t:a:s:n:");
    if(ret == -1){
      break;
    }
    switch (ret){
      case 'i':
	opt.device_name = strdup(optarg);
	break;
      case 't':
	if (!strcmp(optarg, "fanout"))
	  opt.capture_type = CAPTURE_W_FANOUT;
	else if (!strcmp(optarg, "udpstream"))
	  opt.capture_type = CAPTURE_W_UDPSTREAM;
	else if (!strcmp(optarg, "TODO"))
	  opt.capture_type = CAPTURE_W_TODO;
	//TODO: Add if other capture types added
	else {
	  fprintf(stderr, "Unknown packet capture type [%s]\n", optarg);
	  usage(argv[0]);
	  exit(1);
	}
	break;
      case 'a':
	if (!strcmp(optarg, "hash"))
	  opt.fanout_type = PACKET_FANOUT_HASH;
	else if (!strcmp(optarg, "lb"))
	  opt.fanout_type = PACKET_FANOUT_LB;
	else {
	  fprintf(stderr, "Unknown fanout type [%s]\n", optarg);
	  usage(argv[0]);
	  exit(1);
	}
	break;
      case 's':
	opt.port = atoi(optarg);
	break;
      case 'n':
	opt.n_threads = atoi(optarg);
	break;
      default:
	usage(argv[0]);
	exit(1);
    }
  }
  if(argc -optind != 2){
    usage(argv[0]);
    exit(1);
  }
  argv +=optind;
  argc -=optind;
  opt.filename = argv[0];
  opt.points = (struct rec_point *)calloc(opt.n_threads, sizeof(struct rec_point));
  //TODO: read diskspots from config file. Hardcoded for testing
  for(i=0;i<opt.n_threads;i++){
    opt.points[i].filename = (char*)malloc(FILENAME_MAX);
    sprintf(opt.points[i].filename, "%s%d%s%s", "/mnt/disk", i, "/", opt.filename);
  }
  opt.time = atoi(argv[1]);
  if (opt.rec_type == WRITER_AIOW_RBUF)
    opt.f_flags = O_WRONLY|O_DIRECT|O_NOATIME|O_NONBLOCK;
}
int main(int argc, char **argv)
{
  int i;

#ifdef DEBUG_OUTPUT
  fprintf(stdout, "STREAMER: Reading parameters\n");
#endif
  parse_options(argc,argv);

  /*
     switch(opt.capture_type){
     case CAPTURE_W_FANOUT:
     n_threads = THREADS;
     break;
     case CAPTURE_W_UDPSTREAM:
     n_threads = UDP_STREAM_THREADS;
     break;
     }
     */
  struct streamer_entity threads[opt.n_threads];
  pthread_t pthreads_array[opt.n_threads];
  struct stats stats;
  //pthread_attr_t attr;
  int rc;
#ifdef TUNE_AFFINITY
  long processors = sysconf(_SC_NPROCESSORS_ONLN);
#endif

#ifdef TUNE_AFFINITY
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
#endif
  //for(j = 0;j<6;j++)
  //CPU_SET(j,&cpuset);
  //device_name = argv[1];
  //fanout_id = getpid() & 0xffff;

  //pthread_attr_init(&attr);
  //pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
  //Init all threads
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "STREAMER: Initializing threads\n");
#endif
  for(i=0;i<opt.n_threads;i++){
    int err = 0;
    struct recording_entity * se = (struct recording_entity*)malloc(sizeof(struct streamer_entity));

    //Init record points
    err = init_recpoint(&(opt.points[i]), &opt);
    if(err < 0){
      fprintf(stderr, "Error in recpoint init\n");
      exit(-1);
    }
    se->rp = &(opt.points[i]);

    //Initialize recorder entity
    switch(opt.rec_type)
    {
      case WRITER_AIOW_RBUF:
	//Helper function
	err = rbuf_init_rec_entity(se);
	break;
      case WRITER_TODO:
	//Implement own writer here
	break;
    }
    if(err < 0){
      fprintf(stderr, "Error in buffer/writer init\n");
      exit(-1);
    }


    switch(opt.capture_type)
    {
      /*
       * When adding a new capture technique add it here
       */
      case CAPTURE_W_FANOUT:
	threads[i].open = setup_socket;
	threads[i].start = fanout_thread;
	threads[i].close = close_fanout;
	threads[i].opt = threads[i].open(&opt, se);
	break;
      case CAPTURE_W_UDPSTREAM:
	threads[i].open = setup_udp_socket;
	threads[i].start = udp_streamer;
	threads[i].close = close_udp_streamer;
	threads[i].opt = threads[i].open(&opt, se);
	break;
      case CAPTURE_W_TODO:
	fprintf(stderr, "Not yet implemented");
	exit(0);
    }
    if(threads[i].opt == NULL){
      fprintf(stderr, "Error in thread init\n");
      exit(-1);
    }

  }
  for(i=0;i<opt.n_threads;i++){
#ifdef DEBUG_OUTPUT
    printf("STREAMER: In main, starting thread %d\n", i);
#endif

    //TODO: Check how to poll this from system
    rc = pthread_create(&pthreads_array[i], NULL, threads[i].start, threads[i].opt);
    if (rc){
      printf("ERROR; return code from pthread_create() is %d\n", rc);
      exit(-1);
    }
    //Spread processes out to n cores
    //NOTE: setaffinity should be used after thread has been started
#ifdef TUNE_AFFINITY
    CPU_SET(i%processors,&cpuset);

    rc = pthread_setaffinity_np(pthreads_array[i], sizeof(cpu_set_t), &cpuset);
    if(rc != 0)
      printf("Error: setting affinity");
    CPU_ZERO(&cpuset);
#endif

  }

  init_stat(&stats);
  //sleep(opt.time);
  for (i = 0; i < opt.n_threads; i++) {
    rc = pthread_join(pthreads_array[i], NULL);
    if (rc) {
      printf("ERROR; return code from pthread_join() is %d\n", rc);
      exit(-1);
    }
    //get_stats(threads[i].opt, &stats);
    //printf("Main: completed join with thread %ld having a status of %ld\n",t,(long)status);
  }
  //Close all threads
  for(i=0;i<opt.n_threads;i++){
    threads[i].close(threads[i].opt, &stats);
  }
  free(opt.points);
  print_stats(&stats, &opt);

  //return 0;
  pthread_exit(NULL);
}
