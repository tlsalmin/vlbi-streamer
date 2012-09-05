#ifndef STREAMER
#define STREAMER
 	
#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else /* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif /* __cplusplus */



//Rate as in GB/s
//Made an argument and changed to MB/s
//#define RATE 10
#define B(x) (1 << x)
#define AIO_END_OF_FILE -323

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define BILLION 1E9l
#define MILLION 1E6l

#define KB 1024
#define BYTES_TO_MBITSPS(x)	(x*8)/(KB*KB)

#define MUTEX_FREE		1

/* What buf entity to use. Used by buf_type*/ 
#define LOCKER_WRITER		0x0000000f 
#define BUFFER_RINGBUF 		B(0)
#define WRITER_DUMMY 		B(1)
#define BUFFER_SIMPLE		B(2)

/* What HD writer to use. Used by rec_type*/
#define LOCKER_REC		0x000000f0
#define REC_AIO 		B(4)
#define REC_DUMMY 		B(5)
#define REC_DEF 		B(6)
#define REC_SPLICER 		B(7)

/* How to capture packets. */
#define LOCKER_CAPTURE		0x00000f00
#define CAPTURE_W_FANOUT 	B(8)
#define CAPTURE_W_UDPSTREAM 	B(9)
#define CAPTURE_W_SPLICER 	B(10)
#define CAPTURE_W_TODO 		B(11)

/* How fanout works */
/*
#define LOCKER_FANOUT		0x0000f000
#define PACKET_FANOUT_HASH     	0x01 << 12
#define PACKET_FANOUT_LB        0x01 << 13
*/

/* Global stuff */
//#define CHECK_SEQUENCE 		B(12)
#define ASYNC_WRITE		B(13)
#define READMODE		B(14)
#define USE_HUGEPAGE		B(15)

//#define WAIT_BETWEEN		B(16)
#define VERBOSE			B(17)
#define MOUNTPOINT_VERBOSE	B(18)
#define SIMPLE_BUFFER		B(19)

#define USE_RX_RING		B(20)
#define LIVE_SENDING		B(21)
#define LIVE_RECEIVING		B(22)
/* Empty B(21) B(22) B(23)	*/

#define LOCKER_DATATYPE		0x0f000000
#define	DATATYPE_UNKNOWN	B(24) 
#define	DATATYPE_VDIF		B(25) 
#define	DATATYPE_MARK5B		B(26) 
#define DATATYPE_UDPMON		B(27)

#define MEG			B(20)
#define GIG			B(30)

#define STATUS_NOT_STARTED	B(0)
#define STATUS_RUNNING		B(1)
#define	STATUS_STOPPED		B(2)
#define STATUS_FINISHED		B(3)
#define STATUS_ERROR		B(4)
#define STATUS_CANCELLED	B(5)

#define BRANCHOP_STOPANDSIGNAL 1
#define BRANCHOP_GETSTATS 2
#define BRANCHOP_CLOSERBUF 3
#define BRANCHOP_CLOSEWRITER 4
#define BRANCHOP_WRITE_CFGS 5
#define BRANCHOP_READ_CFGS 6
#define BRANCHOP_CHECK_FILES 7

//Stolen from Harro Verkouters jive5ab
#define MK5B_FRAME_WORDS	2504
#define MK5B_FRAME_SIZE		(MK5B_FRAME_WORDS * sizeof(uint32_t))

#define VDIF_SECOND_BITSHIFT 24
/* Grab the last 30 bits of the first 4 byte word 	*/
#define GET_VDIF_SECONDS(buf) (0x3fffffff & ((uint32_t*)(buf)))
#define GET_VDIF_SSEQ(buf) (0x00ffffff & (((uint32_t*)(buf))+1))
#define GET_VDIF_SEQNUM(seconds, sseq) ((unsigned long)((((unsigned long)(seconds))<<VDIF_SECOND_BITSHIFT) | ((unsigned long)(sseq))))
#define GET_VDIF_SEQ(buf) GET_VDIF_SEQNUM(GET_VDIF_SECONDS(buf), GET_VDIF_SSEQ(buf))
#define GET_VDIF_SECONDS_FROM_SEQNUM(seqnum) (buf) >> VDIF_SECOND_BITSHIFT
/* Idea for seqnuming: Have a buf first number, which grounds 	*/
/* frame. If get_spot is negative, it belongs to the previous	*/
/* buffer. If larger than buf, belongs to the next		*/



//Moved to HAVE_HUGEPAGES
//#define HAVE_HUGEPAGES
//#define WRITE_WHOLE_BUFFER
//
//#define ROOTDIRS "/mnt/disk"
#define INITIAL_N_FILES B(7)

#define SIMPLE_BUFCACL
#ifdef SIMPLE_BUFCACL
#define CALC_BUF_SIZE(x) calculate_buffer_sizes_simple(x)
#else
define CALC_BUF_SIZE(x) calculate_buffer_sizes(x)
#endif

#define MIN_MEM_GIG 4l
#define MAX_MEM_GIG 12l
  /* TODO query this */
#define BLOCK_ALIGN 4096
  //#define MAX_MEM_GIG 8

  /* Default lenght of index following file as in <filename>.[0-9]8 */
#define INDEXING_LENGTH 8

  /* Default packet size */
#define DEF_BUF_ELEM_SIZE 8192
  //#define BUF_ELEM_SIZE 32768
#define MAX_OPEN_FILES 48
  //#define MADVISE_INSTEAD_OF_O_DIRECT

  /* Send stuff to log file if daemon mode defined 	*/
#define LOG(...) fprintf(stdout, __VA_ARGS__)
#define LOGERR(...) fprintf(stderr, __VA_ARGS__)
#define D(str, ...)\
    do { if(DEBUG_OUTPUT) fprintf(stdout,"%s:%d:%s(): " str "\n",__FILE__,__LINE__,__func__ __VA_ARGS__); } while(0)
#define E(str, ...)\
    do { fprintf(stderr,"ERROR: %s:%d:%s(): " str "\n",__FILE__,__LINE__,__func__ __VA_ARGS__ ); } while(0)

#define DEBUG_OUTPUT_2 0
#define DD(str, ...) if(DEBUG_OUTPUT_2)D(str, __VA_ARGS__)

#define CHECK_AND_EXIT(x) do { if(x == NULL){ E("Couldn't get any x so quitting"); pthread_exit(NULL); } } while(0)
#define INIT_ERROR return -1;
#define CHECK_ERR_CUST(x,y) do{if(y!=0){perror(x);E("ERROR:"x);return -1;}else{D(x);}}while(0)
#define CHECK_ERR_CUST_QUIET(x,y) do{if(y!=0){perror(x);E("ERROR:"x);return -1;}}while(0)
#define CHECK_ERR(x) CHECK_ERR_CUST(x,err)
#define CHECK_ERR_QUIET(x) CHECK_ERR_CUST_QUIET(x,err)
#define CHECK_ERRP_CUST(x,y) do{if(y!=0){perror(x);E("ERROR:"x);pthread_exit(NULL);}else{D(x);}}while(0)
#define CHECK_ERRP(x) CHECK_ERRP_CUST(x,err)
#define CHECK_ERR_NONNULL(val,mes) do{if(val==NULL){perror(mes);E(mes);return -1;}else{D(mes);}}while(0)
#define SILENT_CHECK_ERR_LTZ(x) do{if(err<0){perror(x);E(x);return -1;}}while(0)
#define SILENT_CHECK_ERRP_LTZ(x) do{if(err<0){perror(x);E(x);pthread_exit(NULL);}}while(0)
#define CHECK_LTZ(x,y) do{if(y<0){perror(x);E(x);return -1;}else{D(x);}}while(0)
#define CHECK_ERR_LTZ(x) CHECK_LTZ(x,err)
#define CALL_AND_CHECK(x,...)\
    err = x(__VA_ARGS__);\
  CHECK_ERR(#x);



  //Moved to configure
  //#define DEBUG_OUTPUT
  //Magic number TODO: Refactor so we won't need this
#define WRITE_COMPLETE_DONT_SLEEP 1337
  /* The length of our indices. A week at 10Gb/s is 99090432000 packets for one thread*/
#define INDEX_FILE_TYPE unsigned long
  /* TODO: Make the definition below work where used */
#define INDEX_FILE_PRINT lu
  /* Moving the rbuf-stuff to its own thread */
#define SPLIT_RBUF_AND_IO_TO_THREAD

  //#define TUNE_AFFINITY
  //#define PRIORITY_SETTINGS


  /* Enable if you don't want extra messaging to nonblocked processes */
  //#define CHECK_FOR_BLOCK_BEFORE_SIGNAL

  //NOTE: Weird behaviour of libaio. With small integer here. Returns -22 for operation not supported
  //But this only happens on buffer size > (atleast) 30000
  //Lets make it write every 65536 KB(4096 byte aligned)(TODO: Increase when using write and read at the same time)
  //Default write size as 16MB
#define HD_MIN_WRITE_SIZE 16777216
  //Default file size as 500MB
#define FILE_SPLIT_TO_BLOCKS B(29)l
  //#define HD_MIN_WRITE_SIZE 1048576
  /* Size of current default huge page */
  //#define HD_MIN_WRITE_SIZE 2097152
  //#define HD_MIN_WRITE_SIZE 134217728
  //#define HD_MIN_WRITE_SIZE 33554432
  //#define HD_MIN_WRITE_SIZE 262144
  //#define HD_WRITE_SIZE 524288
  //#define HD_MIN_WRITE_SIZE 65536
  /* Tested with misc/bytaligntest.c that dividing the buffer 	*/
  /* to 16 blocks gives a good byte aling and only doesn't work	*/
  /* on crazy sized packets like 50kB+ */
  //#define MAGIC_BUFFERDIVISION 16
#define MIN(x,y) (x < y ? x : y)

  //#define DO_W_STUFF_EVERY (HD_WRITE_SIZE/BUF_ELEM_SIZE)
  //etc for packet handling
#include <pthread.h>
#include <config.h>
#ifdef HAVE_LIBCONFIG_H
#include <libconfig.h>
#endif
#include <netdb.h> // struct hostent
#include "config.h"
#include "resourcetree.h"
#include "timer.h"
  struct stats
{
  unsigned long total_packets;
  unsigned long total_bytes;
  unsigned long total_written;
  unsigned long incomplete;
  unsigned long dropped;
  //unsigned long files_exchanged;
  //Cheating here to keep infra consistent
  //int * packet_index;
};
struct rxring_request{
  long unsigned* id;
  int* bufnum;
};
struct fileholder
{
  long unsigned id;
  int diskid;
  int status;
  struct fileholder* next;
};
#define FH_ONDISK	B(0)
#define FH_MISSING	B(1)
#define FH_INMEM	B(2)
#define FH_BUSY		B(3)
void zero_fileholder(struct fileholder* fh);
/* All the options for the main thread			*/
struct opt_s
{
  char *filename;

  /* Lock that spans over all threads. Used for tracking files	 	*/
  /* by sequence number							*/
  long unsigned *cumul;
  /* Used in read to determine how many we actually found 		*/
  long unsigned cumul_found;
  //pthread_mutex_t cumlock;
  char *device_name;
  char *cfgfile;
  char *logfile;
  int diskids;

  /* Make this a spinlock, since augmenting this struct is fast		*/
  pthread_spinlock_t *augmentlock;
  //unsigned long n_files;
  //struct fileblocks *fbs;
  struct opt_s* liveother;
  unsigned int optbits;
  int root_pid;
  int hd_failures;
  unsigned long time;
  int port;
  unsigned long minmem;
  unsigned long maxmem;
  int socket;
  struct entity_list_branch *membranch;
  struct entity_list_branch *diskbranch;
  int n_threads;
  int n_drives;
  int bufculum;
  int rate;
  //int* fileholders;
  struct fileholder * fileholders;
  /* Used if RX-ring for receive */
  void* buffer;
#ifdef HAVE_LIBCONFIG_H
  config_t cfg;
#endif

  unsigned long do_w_stuff_every;
#ifdef HAVE_RATELIMITER
  int wait_nanoseconds;
  TIMERTYPE wait_last_sent;
  TIMERTYPE start_time;
#endif
  //unsigned long max_num_packets;
  char * filenames[MAX_OPEN_FILES];
  struct timespec starting_time;
  //unsigned long filesize;

  /* Moved to optbits */
  //int capture_type;
  //int buf_type;
  //int rec_type;
  //int fanout_type;
  //int async;
  //int read;
  //int handle;

  /* Bloat TODO Find alternative place for this */
  INDEX_FILE_TYPE packet_size;
  int buf_num_elems;
  int buf_division;
  //These two are a bit silly. Should be moved to use as a parameter
  int taken_rpoints;
  int tid;
  char * hostname;
  unsigned long serverip;
  //pthread_cond_t signal;
  //struct hostent he;
  //int f_flags;
#if(DAEMON)
  void (*get_stats)(void*, void*);
  long unsigned bytes_exchanged;
#endif
  unsigned long total_packets;
  pthread_t *rbuf_pthreads;
  struct buffer_entity * bes;
  struct recording_entity *recs;
#ifdef PRIORITY_SETTINGS
  pthread_attr_t        pta;
  struct sched_param    param;
#endif
#ifdef TUNE_AFFINITY
  cpu_set_t cpuset;
#endif
#if(DAEMON)
  int status;
#endif
  struct streamer_entity * streamer_ent;
};
int parse_options(int argc, char **argv, struct opt_s* opt);
int clear_and_default(struct opt_s* opt, int create_cfg);
int clear_pointers(struct opt_s* opt);
int remove_specific_from_fileholders(struct opt_s *opt, unsigned long id);
struct buffer_entity
{
  void * opt;
  //Functions for usage in modularized infrastructure
  /* TODO: This is getting bloated. Check what we're actually still using */
  int (*init)(struct opt_s* , struct buffer_entity*);
  int (*write)(struct buffer_entity*,int);
  void* (*get_writebuf)(struct buffer_entity *);
  /* Used to acquire element past the queue line */
  int (*acquire)(void * , void* , void*);
  void* (*simple_get_writebuf)(struct buffer_entity *, int **);
  int* (*get_inc)(struct buffer_entity *);
  void (*set_ready)(struct buffer_entity*);
  void (*cancel_writebuf)(struct buffer_entity *);
  int (*wait)(struct buffer_entity *);
  int (*close)(struct buffer_entity*,void * );
  void* (*write_loop)(void *);
  void (*stop)(struct buffer_entity*);
  void (*init_mutex)(struct buffer_entity *, void*,void*);
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
  int (*is_blocked)(struct buffer_entity*);
#endif
  //int (*handle_packet)(struct buffer_entity*, void *);
  struct recording_entity * recer;
  //struct streamer_entity * se;
  /* used to set the writer to free */
  struct listed_entity * self;
  //struct entity_list_branch *membranch;
  //struct entity_list_branch *diskbranch;
  //struct rec_point * rp;

  LOCKTYPE *headlock;
  pthread_cond_t *iosignal;
};
struct recording_entity
{
  void * opt;
  int (*init)(struct opt_s * , struct recording_entity*);
  long (*write)(struct recording_entity*,void*,size_t);
  int (*wait)(struct recording_entity *);
  int (*close)(struct recording_entity*, void *);
  long (*check)(struct recording_entity*, int );
  int (*getfd)(struct recording_entity*);
  int (*getid)(struct recording_entity*);
  int (*check_files)(struct recording_entity*, void*);
  void (*get_stats)(void*, void*);

  int (*writecfg)(struct recording_entity *, void*);
  int (*readcfg)(struct recording_entity *, void*);

  int (*get_w_flags)();
  int (*handle_error)(struct recording_entity *, int);
  int (*get_r_flags)();
  const char* (*get_filename)(struct recording_entity *re);
  /* Bloat bloat bloat. TODO: Add a common filestruct or something*/
  unsigned long (*get_n_packets)(struct recording_entity*);
  INDEX_FILE_TYPE* (*get_packet_index)(struct recording_entity*);
  struct buffer_entity *be;
  /* Used to set the buffer free */
  struct listed_entity *self;
  /* Used to query for a free writer and free self */
  //struct entity_list_branch *diskbranch;
};

//Generic struct for a streamer entity
struct streamer_entity
{
  void *opt;
  int (*init)(struct opt_s *, struct streamer_entity *se);
  void* (*start)(void*);
  int (*close)(void*,void*);
  void (*stop)(struct streamer_entity *se);
  void (*close_socket)(struct streamer_entity *se);
  /* Added to get periodic stats */
  void (*get_stats)(void*, void*);
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
  int (*is_blocked)(struct streamer_entity *se);
#endif
  int (*is_running)(struct streamer_entity *se);
  unsigned long (*get_max_packets)(struct streamer_entity *se);
  /* TODO: Refactor streamer to use the same syntax as buffer and writer */
  struct buffer_entity *be;
  struct listed_entity *rbuf;
  //struct entity_list_branch *membranch;
};

/* TODO: Doc these */
int init_rbufs(struct opt_s *opt);
int close_rbufs(struct opt_s *opt, struct stats* da_stats);
int close_opts(struct opt_s *opt);
int init_recp(struct opt_s *opt);
int close_recp(struct opt_s *opt, struct stats* da_stats);
int calculate_buffer_sizes(struct opt_s *opt);
int calculate_buffer_sizes_simple(struct opt_s * opt);
void init_stats(struct stats *stats);
void neg_stats(struct stats* st1, struct stats* st2);
void add_stats(struct stats* st1, struct stats* st2);
//Timerstuff
int close_streamer(struct opt_s *opt);
int init_branches(struct opt_s *opt);
void shutdown_thread(struct opt_s *opt);
#if(DAEMON)
void* vlbistreamer(void *opti);
#endif
int iden_from_opt(struct opt_s *opt, void* val1, void* val2, int iden_type);



#endif
