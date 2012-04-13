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
#include <sys/resource.h> /*Query max allocatable memory */
//TODO: Add explanations for includes
#include <netdb.h> // struct hostent
#include "config.h"
#include "streamer.h"
#include "fanout.h"
#include "udp_stream.h"
#include "ringbuf.h"
#ifdef HAVE_LIBAIO
#include "aiowriter.h"
#endif
#include "common_wrt.h"
#include "defwriter.h"
#include "sendfile_streamer.h"
#include "splicewriter.h"
#define TUNE_AFFINITY

#define CORES 6
extern char *optarg;
extern int optind, optopt;


static struct opt_s opt;

/*
 * Adapted from http://coding.debuntu.org/c-linux-socket-programming-tcp-simple-http-client
 */
int resolve_host(char *host, struct in_addr * ia){
  int err=0;
  return err;
}

/*
 * Stuff stolen from lindis sendfileudp
 */
static void usage(char *binary){
  fprintf(stderr, 
      "usage: %s [OPTION]... name (time to receive / host to send to)\n"
      "-i INTERFACE	Which interface to capture from\n"
      "-t {fanout|udpstream|sendfile|TODO	Capture type(Default: udpstream)\n"
      //"-a {lb|hash}	Fanout type(Default: lb)\n"
      "-n NUM	        Number of threads(Required)\n"
      "-s SOCKET	Socket number(Default: 2222)\n"
#ifdef HAVE_HUGEPAGES
      "-u 		Use hugepages\n"
#endif
      "-m {s|r}		Send or Receive the data(Default: receive)\n"
      "-p SIZE		Set buffer size to SIZE(Needs to be aligned with sent packet size)\n"
      "-r RATE		Expected network rate in MB(default: 10000)\n"
      "-w {"
#ifdef HAVE_LIBAIO
      "aio|"
#endif
      "def|splice|dummy}	Choose writer to use(Default: defwriter)\n"
#ifdef CHECK_OUT_OF_ORDER
      "-q 		Check if packets are in order from first 64bits of package\n"
#endif
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
  if(opts->optbits & READMODE){
//TODO
  }
  else{
  fprintf(stdout, "Stats for %s \n"
      "Packets: %lu\n"
      "Bytes: %lu\n"
      "Dropped: %lu\n"
      "Incomplete: %lu\n"
      "Written: %lu\n"
      "Time: %d\n"
      "Net receive Speed: %luMb/s\n"
      "HD write Speed: %luMb/s\n"
      ,opts->filename, opts->cumul, stats->total_bytes, stats->dropped, stats->incomplete, stats->total_written,opts->time, (stats->total_bytes*8)/(1024*1024*opts->time), (stats->total_written*8)/(1024*1024*opts->time) );
  }
}
static void parse_options(int argc, char **argv){
  int ret,i;

  memset(&opt, 0, sizeof(struct opt_s));
  opt.filename = NULL;
  opt.device_name = NULL;

  /* Opts using optbits */
  //opt.capture_type = CAPTURE_W_FANOUT;
  opt.optbits |= CAPTURE_W_UDPSTREAM;
  //opt.fanout_type = PACKET_FANOUT_LB;
  //opt.optbits |= PACKET_FANOUT_LB;
  opt.root_pid = getpid();
  opt.port = 2222;
  opt.n_threads = 1;
  opt.buf_elem_size = BUF_ELEM_SIZE;
  //TODO: Add option for choosing backend
  //opt.buf_type = BUFFER_RINGBUF;
  opt.optbits |= BUFFER_RINGBUF;
  //opt.rec_type= REC_DEF;
  opt.optbits |= REC_DEF;
  opt.taken_rpoints = 0;
  opt.rate = 10000;
  //opt.handle = 0;
  //opt.read = 0;
  opt.tid = 0;
  //opt.async = 0;
  //opt.optbits = 0xff000000;
  opt.socket = 0;
  while((ret = getopt(argc, argv, "i:t:s:n:m:w:p:qur:"))!= -1){
    switch (ret){
      case 'i':
	opt.device_name = strdup(optarg);
	break;
      case 't':
	opt.optbits &= ~LOCKER_CAPTURE;
	if (!strcmp(optarg, "fanout")){
	  //opt.capture_type = CAPTURE_W_FANOUT;
	  opt.optbits |= CAPTURE_W_FANOUT;
	}
	else if (!strcmp(optarg, "udpstream")){
	  //opt.capture_type = CAPTURE_W_UDPSTREAM;
	  opt.optbits |= CAPTURE_W_UDPSTREAM;
	}
	else if (!strcmp(optarg, "sendfile")){
	  //opt.capture_type = CAPTURE_W_SPLICER;
	  opt.optbits |= CAPTURE_W_SPLICER;
	}
	else {
	  fprintf(stderr, "Unknown packet capture type [%s]\n", optarg);
	  usage(argv[0]);
	  exit(1);
	}
	break;
	/* Fanout choosing removed and set to default LB since
	 * Implementation not that feasible anyway
      case 'a':
	opt.optbits &= ~LOCKER_FANOUT;
	if (!strcmp(optarg, "hash")){
	  //opt.fanout_type = PACKET_FANOUT_HASH;
	  opt.optbits |= PACKET_FANOUT_HASH;
	  }
	else if (!strcmp(optarg, "lb")){
	  //opt.fanout_type = PACKET_FANOUT_LB;
	  opt.optbits |= PACKET_FANOUT_LB;
	}
	else {
	  fprintf(stderr, "Unknown fanout type [%s]\n", optarg);
	  usage(argv[0]);
	  exit(1);
	}
	break;
	*/
      case 'r':
	opt.rate = atoi(optarg);
	break;
      case 's':
	opt.port = atoi(optarg);
	break;
      case 'p':
	opt.buf_elem_size = atoi(optarg);
	break;
      case 'u':
#ifdef HAVE_HUGEPAGES
	opt.optbits |= USE_HUGEPAGE;
#endif
	break;
      case 'n':
	opt.n_threads = atoi(optarg);
	break;
      case 'q':
#ifdef CHECK_OUT_OF_ORDER
	//opt.handle |= CHECK_SEQUENCE;
	opt.optbits |= CHECK_SEQUENCE;
	break;
#endif
      case 'm':
	if (!strcmp(optarg, "r")){
	  opt.optbits &= ~READMODE;
	  //opt.read = 0;
	}
	else if (!strcmp(optarg, "s")){
	  //opt.read = 1;
	  opt.optbits |= READMODE;
	}
	else {
	  fprintf(stderr, "Unknown mode type [%s]\n", optarg);
	  usage(argv[0]);
	  exit(1);
	}
	break;
      case 'w':
	opt.optbits &= ~LOCKER_REC;
	if (!strcmp(optarg, "def")){
	  /*
	  opt.rec_type = REC_DEF;
	  opt.async = 0;
	  */
	  opt.optbits |= REC_DEF;
	  opt.optbits &= ~ASYNC_WRITE;
	}
#ifdef HAVE_LIBAIO
	else if (!strcmp(optarg, "aio")){
	  /*
	  opt.rec_type = REC_AIO;
	  opt.async = 1;
	  */
	  opt.optbits |= REC_AIO|ASYNC_WRITE;
	}
#endif
	else if (!strcmp(optarg, "splice")){
	  /*
	  opt.rec_type = REC_SPLICER;
	  opt.async = 0;
	  */
	  opt.optbits |= REC_SPLICER;
	  opt.optbits &= ~ASYNC_WRITE;
	}
	else if (!strcmp(optarg, "dummy")){
	  /*
	  opt.rec_type = REC_DUMMY;
	  opt.buf_type = WRITER_DUMMY;
	  */
	  opt.optbits &= ~LOCKER_WRITER;
	  opt.optbits |= REC_DUMMY|WRITER_DUMMY;
	  opt.optbits &= ~ASYNC_WRITE;
	}
	else {
	  fprintf(stderr, "Unknown mode type [%s]\n", optarg);
	  usage(argv[0]);
	  exit(1);
	}
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
  if(opt.optbits & READMODE)
    opt.hostname = argv[1];
  else
    opt.time = atoi(argv[1]);
  opt.cumul = 0;
  /* Calc the max per thread amount of packets we can receive */
  /* TODO: Systems with non-uniform diskspeeds stop writing after too many packets */
  if(!(opt.optbits & READMODE)){
    loff_t prealloc_bytes = (((unsigned long)opt.rate)*opt.time*1024)/(opt.buf_elem_size);
    //Split kb/gb stuff to avoid overflow warning
    prealloc_bytes = (prealloc_bytes*1024*8)/opt.n_threads;
    opt.max_num_packets = prealloc_bytes;
  }

  //Set buf_size
  //OK set limiter so that when using one thread, we won't actually allocate 4GB of mem
  opt.do_w_stuff_every = HD_WRITE_SIZE/opt.buf_elem_size;

  
  int threads = opt.n_threads;
  /*
  if(opt.n_threads < 4)
    threads = 4;
  else
  */
    //threads = opt.n_threads;
  struct rlimit rl;
  /* Query max size */
  /* TODO: Doesn't work properly althought mem seems to be unlimited */
  ret = getrlimit(RLIMIT_DATA, &rl);
  if(ret < 0){
    fprintf(stderr, "Failed to get rlimit of memory\n");
    exit(1);
  }
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "STREAMER: Queried max mem size %ld \n", rl.rlim_cur);
#endif
  unsigned long temp = (unsigned long)MEM_GIG*GIG;
  if (temp > rl.rlim_cur && rl.rlim_cur != RLIM_INFINITY){
#ifdef DEBUG_OUTPUT
    fprintf(stdout, "STREAMER: Limiting memory to %lu\n", rl.rlim_cur);
#endif
    temp = rl.rlim_cur;
  }
  temp = (temp)/((long)opt.buf_elem_size*(long)threads);
  opt.buf_num_elems = (int)temp;
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "STREAMER: Elem num in single buffer: %d. single buffer size : %ld bytes\n", opt.buf_num_elems, ((long)opt.buf_num_elems*(long)opt.buf_elem_size));
#endif
  /*
     if (opt.rec_type == REC_AIO)
     opt.f_flags = O_WRONLY|O_DIRECT|O_NOATIME|O_NONBLOCK;
     */
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
  /* Handle hostname etc */
  /* TODO: Whats the best way that accepts any format? */
  if(opt.optbits & READMODE){
    struct hostent *hostptr;

    hostptr = gethostbyname(opt.hostname);
    if(hostptr == NULL){
      perror("Hostname");
      exit(-1);
    }
    memcpy(&(opt.serverip), (char *)hostptr->h_addr, sizeof(opt.serverip));

#ifdef DEBUG_OUTPUT
    fprintf(stdout, "STREAMER: Resolved hostname\n");
#endif
  }

  //Create message queue
  pthread_mutex_init(&(opt.cumlock), NULL);
  pthread_cond_init (&opt.signal, NULL);


#ifdef DEBUG_OUTPUT
  fprintf(stdout, "STREAMER: Initializing threads\n");
#endif
  for(i=0;i<opt.n_threads;i++){
    int err = 0;
    struct buffer_entity * be = (struct buffer_entity*)malloc(sizeof(struct buffer_entity));
    struct recording_entity * re = (struct recording_entity*)malloc(sizeof(struct recording_entity));

    /*
     * NOTE: AIOW-stuff and udp-streamer are bidirectional and
     * only require the setting of opt->read to one for 
     * sending stuff
     */
    switch(opt.optbits & LOCKER_REC){
#if HAVE_LIBAIO
      case REC_AIO:
	err = aiow_init_rec_entity(&opt, re);
	//NOTE: elem_size is read inside if we're reading
	break;
#endif
      case REC_DUMMY:
	err = rec_init_dummy(&opt, re);
	break;
      case REC_DEF:
	err = def_init_def(&opt, re);
	break;
      case REC_SPLICER:
	err = splice_init_splice(&opt, re);
	break;
    }
    if(err < 0){
      fprintf(stderr, "Error in writer init\n");
      exit(-1);
    }
    //Make elements accessible
    be->recer = re;
    re->be = be;

    //Initialize recorder entity
    switch(opt.optbits & LOCKER_WRITER)
    {
      case BUFFER_RINGBUF:
	//Helper function
	err = rbuf_init_buf_entity(&opt, be);
#ifdef DEBUG_OUTPUT
	fprintf(stdout, "Initialized buffer for thread %d\n", i);
#endif
	break;
      case WRITER_DUMMY:
	err = rbuf_init_dummy(&opt, be);
	break;
    }
    if(err < 0){
      fprintf(stderr, "Error in buffer init\n");
      exit(-1);
    }


    switch(opt.optbits & LOCKER_CAPTURE)
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
	if(opt.optbits & READMODE)
	  udps_init_udp_sender(&opt, &(threads[i]), be);
	else
	  udps_init_udp_receiver(&opt, &(threads[i]), be);
	break;
      case CAPTURE_W_SPLICER:
	sendfile_init_writer(&opt, &(threads[i]), be);
	break;
      default:
	fprintf(stdout, "DUR %X\n", opt.optbits);
	break;
	
    }
    if(threads[i].opt == NULL){
      fprintf(stderr, "Error in thread init\n");
      exit(-1);
    }
    be->se = &(threads[i]);
    threads[i].be = be;

  }
  for(i=0;i<opt.n_threads;i++){
//#ifdef DEBUG_OUTPUT
    printf("STREAMER: In main, starting thread %d\n", i);
//#endif

    rc = pthread_create(&pthreads_array[i], NULL, threads[i].start, (void*)&threads[i]);
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
  /* If we're capturing, time the threads and run them down after we're done */
  if(!(opt.optbits & READMODE)){
    sleep(opt.time);
    for(i = 0;i<opt.n_threads;i++){
      threads[i].stop(&(threads[i]));
    }
    threads[0].close_socket(&(threads[0]));
    ////pthread_mutex_destroy(opt.cumlock);
  }


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
    free(threads[i].be->recer);
    free(threads[i].be);
  }
  pthread_mutex_destroy(&(opt.cumlock));
  //free(opt.packet_index);
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "STREAMER: Threads closed\n");
#endif
  print_stats(&stats, &opt);
  if(opt.device_name != NULL)
    free(opt.device_name);

  //return 0;
  //pthread_exit(NULL);
  return 0;
}
