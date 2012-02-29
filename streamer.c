#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "streamer.h"
#include "fanout.h"
#include "udp_stream.h"
#define CAPTURE_W_FANOUT 0
#define CAPTURE_W_UDPSTREAM 1
#define CAPTURE_W_TODO 2

#define CORES 6
extern char *optarg;
extern int optind, optopt;


static struct opt_s opt;
/*
static const char *device_name;
static int fanout_type;
static int fanout_id;
static int capture_type;
*/

/*
 * Stuff stolen from lindis sendfileudp
 */
static void usage(char *binary){
  fprintf(stderr, 
      "usage: %s [OPTION]... name time\n"
      "-i INTERFACE	Which interface to capture from\n"
      "-t {fanout|udpstream|TODO	Capture type(fanout only one available atm)\n"
      "-a {lb|hash}	Fanout type\n"
      "-s SOCKET	Socket number\n"
      ,binary);
}
void init_stat(struct stats *stats){
  stats->total_bytes = 0;
  stats->incomplete = 0;
  stats->total_packets = 0;
  stats->dropped = 0;
}
void print_stats(struct stats *stats, struct opt_s * opts){
  fprintf(stdout, "Stats for %s \n"
      "Packets: %u\n"
      "Bytes: %u\n"
      "Dropped: %u\n"
      "Incomplete: %u\n"
      "Time: %d\n"
      ,opts->filename, stats->total_packets, stats->total_bytes, stats->dropped, stats->incomplete, opts->time );
}
static void parse_options(int argc, char **argv){
  int ret;

  memset(&opt, 0, sizeof(struct opt_s));
  opt.filename = NULL;
  opt.device_name = NULL;
  opt.capture_type = CAPTURE_W_FANOUT;
  opt.fanout_type = PACKET_FANOUT_LB;
  opt.root_pid = getpid();
  opt.socket = 2222;
  for(;;){
    ret = getopt(argc, argv, "i:t:a:s:");
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
	opt.socket = atoi(optarg);
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
  opt.time = atoi(argv[1]);
}
int main(int argc, char **argv)
{
  //int fd, err;
  int i, err, n_threads=1;

  parse_options(argc,argv);

  switch(opt.capture_type){
      case CAPTURE_W_FANOUT:
	n_threads = THREADS;
	break;
      case CAPTURE_W_UDPSTREAM:
	n_threads = 1;
	break;
  }
  struct streamer_entity threads[n_threads];
  pthread_t pthreads_array[n_threads];
  struct stats stats;
  //pthread_attr_t attr;
  int rc;
  long processors = sysconf(_SC_NPROCESSORS_ONLN);

  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  //for(j = 0;j<6;j++)
    //CPU_SET(j,&cpuset);
  //device_name = argv[1];
  //fanout_id = getpid() & 0xffff;

  //pthread_attr_init(&attr);
  //pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
  //Init all threads
  for(i=0;i<n_threads;i++){
    switch(opt.capture_type)
    {
      /*
       * When adding a new capture technique add it here
       */
      case CAPTURE_W_FANOUT:
	threads[i].open = setup_socket;
	threads[i].start = fanout_thread;
	threads[i].close = close_fanout;
	threads[i].opt = threads[i].open((void*)&opt);
	break;
      case CAPTURE_W_UDPSTREAM:
	threads[i].open = setup_udp_socket;
	threads[i].start = udp_streamer;
	threads[i].close = close_udp_streamer;
	threads[i].opt = threads[i].open((void*)&opt);
	break;
      case CAPTURE_W_TODO:
	fprintf(stderr, "Not yet implemented");
	exit(0);
    }

  }
  for(i=0;i<n_threads;i++){
    printf("In main, starting thread %d\n", i);

    //TODO: Check how to poll this from system
    rc = pthread_create(&pthreads_array[i], NULL, threads[i].start, threads[i].opt);
    if (rc){
      printf("ERROR; return code from pthread_create() is %d\n", rc);
      exit(-1);
    }
    //Spread processes out to n cores
    //NOTE: setaffinity should be used after thread has been started
    CPU_SET(i%processors,&cpuset);

    err = pthread_setaffinity_np(pthreads_array[i], sizeof(cpu_set_t), &cpuset);
    if(err != 0)
      printf("Error: setting affinity");
    CPU_ZERO(&cpuset);

  }

  init_stat(&stats);
  //sleep(opt.time);
  for (i = 0; i < n_threads; i++) {
    rc = pthread_join(pthreads_array[i], NULL);
    if (rc) {
      printf("ERROR; return code from pthread_join() is %d\n", rc);
      exit(-1);
    }
    get_stats(threads[i].opt, &stats);
    //printf("Main: completed join with thread %ld having a status of %ld\n",t,(long)status);
  }
  //Close all threads
  for(i=0;i<n_threads;i++){
    threads[i].close(threads[i].opt);
  }
  print_stats(&stats, &opt);

  //return 0;
  pthread_exit(NULL);
}
