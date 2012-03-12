#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <mqueue.h>
#include "streamer.h"
#include "fanout.h"
#include "udp_stream.h"
#include "aioringbuf.h"
#include "aiowriter.h"
#define CAPTURE_W_FANOUT 0
#define CAPTURE_W_UDPSTREAM 1
#define CAPTURE_W_TODO 2
#define TUNE_AFFINITY

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
  //stats->total_packets = 0;
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
      ,opts->filename, opts->cumul, stats->total_bytes, stats->dropped, stats->incomplete, stats->total_written,opts->time, (stats->total_bytes*8)/(1024*1024*opts->time), (stats->total_written*8)/(1024*1024*opts->time) );
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
  opt.buf_elem_size = BUF_ELEM_SIZE;
  opt.buf_num_elems = BUF_NUM_ELEMS;
  //TODO: Add option for choosing backend
  opt.buf_type = WRITER_AIOW_RBUF;
  opt.rec_type= REC_AIO;
  opt.taken_rpoints = 0;
  opt.tid = 0;
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
  //fprintf(stdout, "sizzle: %lu\n", sizeof(char));
  opt.filename = argv[0];
  //opt.points = (struct rec_point *)calloc(opt.n_threads, sizeof(struct rec_point));
  //TODO: read diskspots from config file. Hardcoded for testing
  for(i=0;i<opt.n_threads;i++){
    opt.filenames[i] = malloc(sizeof(char)*FILENAME_MAX);
    //opt.filenames[i] = (char*)malloc(FILENAME_MAX);
    sprintf(opt.filenames[i], "%s%d%s%s", "/mnt/disk", i, "/", opt.filename);
  }
  opt.time = atoi(argv[1]);
  opt.cumul = 0;
  //TODO: Just store as int. Look into going char[2] if needed
  loff_t prealloc_bytes = (RATE*opt.time*1024*1024)/(opt.buf_elem_size);
  //TODO: Make macro
  //Split kb/gb stuff to avoid overflow warning
  prealloc_bytes = (prealloc_bytes*1024)/opt.n_threads;
  //2 bytes per line
  opt.packet_index = (void*)malloc(sizeof(int) * prealloc_bytes);
  /*
  if (opt.rec_type == REC_AIO)
    opt.f_flags = O_WRONLY|O_DIRECT|O_NOATIME|O_NONBLOCK;
    */
}
int main(int argc, char **argv)
{
  int i;
  mqd_t mq;

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
  
  //Create message queue
  mq = mq_open(opt.filename, 0);
  pthread_mutex_init(&(opt.cumlock), NULL);


#ifdef DEBUG_OUTPUT
  fprintf(stdout, "STREAMER: Initializing threads\n");
#endif
  for(i=0;i<opt.n_threads;i++){
    int err = 0;
    struct buffer_entity * be = (struct buffer_entity*)malloc(sizeof(struct buffer_entity));
    struct recording_entity * re = (struct recording_entity*)malloc(sizeof(struct recording_entity));

    switch(opt.rec_type){
      case REC_AIO:
	err = aiow_init_rec_entity(&opt, re);
	break;
      case REC_TODO:
	break;
    }
    if(err < 0){
      fprintf(stderr, "Error in writer init\n");
      exit(-1);
    }
    be->recer = re;

    //Initialize recorder entity
    switch(opt.buf_type)
    {
      case WRITER_AIOW_RBUF:
	//Helper function
	err = rbuf_init_buf_entity(&opt, be);
#ifdef DEBUG_OUTPUT
	fprintf(stderr, "Initialized buffer for thread %d\n", i);
#endif
	break;
      case WRITER_TODO:
	//Implement own writer here
	break;
    }
    if(err < 0){
      fprintf(stderr, "Error in buffer init\n");
      exit(-1);
    }


    switch(opt.capture_type)
    {
      /*
       * When adding a new capture technique add it here
       */
      case CAPTURE_W_FANOUT:
	threads[i].init = setup_socket;
	threads[i].start = fanout_thread;
	threads[i].close = close_fanout;
	threads[i].opt = threads[i].init(&opt, be);
	break;
      case CAPTURE_W_UDPSTREAM:
	threads[i].init = setup_udp_socket;
	threads[i].start = udp_streamer;
	threads[i].close = close_udp_streamer;
	threads[i].opt = threads[i].init(&opt, be);
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
  for (i = 0; i < opt.n_threads; i++) {
    rc = pthread_join(pthreads_array[i], NULL);
    if (rc<0) {
      printf("ERROR; return code from pthread_join() is %d\n", rc);
    }
  }
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "STREAMER: Threads finished. Getting stats\n");
#endif
  //Close all threads. Buffers and writers are closed in the threads close
  for(i=0;i<opt.n_threads;i++){
    threads[i].close(threads[i].opt, &stats);
  }
  //TODO: write packet_index to file
  free(opt.packet_index);
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "STREAMER: Threads closed\n");
#endif
  print_stats(&stats, &opt);

  //return 0;
  //pthread_exit(NULL);
  exit(0);
}
