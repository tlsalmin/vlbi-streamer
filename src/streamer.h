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
#define CHECK_SEQUENCE 		B(12)
#define ASYNC_WRITE		B(13)
#define READMODE		B(14)
#define USE_HUGEPAGE		B(15)

#define WAIT_BETWEEN		B(16)
#define VERBOSE			B(17)
#define MOUNTPOINT_VERBOSE	B(18)
#define SIMPLE_BUFFER		B(19)

#define USE_RX_RING		B(20)

#define MEG			B(20)
#define GIG			B(30)

//Moved to HAVE_HUGEPAGES
//#define HAVE_HUGEPAGES
//#define WRITE_WHOLE_BUFFER
//
#define ROOTDIRS "/mnt/disk"
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

#define CHECK_CFG_CUSTOM(x,y) do{if(y == CONFIG_FALSE){E(x);return -1;}else{D(x);}}while(0)
#define CHECK_CFG(x) CHECK_CFG_CUSTOM(x,err)
#define SET_I64(x,y) do{setting = config_lookup(cfg, x); CHECK_ERR_NONNULL(setting,"Set "x); err = config_setting_set_int64(setting,y); CHECK_CFG(x);}while(0) 	
#define GET_I64(x,y) do{setting = config_lookup(cfg, x); CHECK_ERR_NONNULL(setting,"Get "x); y = config_setting_get_int64(setting);D("Got "x" is: %lu",,y);}while(0) 	
#define SET_I(x,y) do{setting = config_lookup(cfg, x); CHECK_ERR_NONNULL(setting,"Set "x); err = config_setting_set_int(setting,y); CHECK_CFG(x);}while(0) 	
#define GET_I(x,y) do{setting = config_lookup(cfg, x); CHECK_ERR_NONNULL(setting,"Get "x); y = config_setting_get_int(setting);D("Got "x" is: %d",,y);}while(0) 	

#define OPT(x) opt->x
#define CFG_ELIF(x) else if(strcmp(config_setting_name(setting), x)==0)
//#define CFG_GET_STR config_setting_get_string(setting)
//#define CFG_GET_INT64 config_setting_get_int64(setting)
#define CFG_CHK_STR(x) \
if(check==1){	\
  if(strcmp(config_setting_get_string(setting),OPT(x)) != 0)\
    return -1;\
}
#define CFG_WRT_STR(x) \
else if(write==1){\
  err = config_setting_set_string(setting,OPT(x));\
  CHECK_CFG(#x);\
}
#define CFG_GET_STR(x) \
else{\
  const char * temp = config_setting_get_string(setting);\
  if(temp != NULL)\
    OPT(x) = strdup(temp);\
  else\
    return -1;\
}
#define CFG_CHK_UINT64(x) \
if(check==1){\
  if(((unsigned long)config_setting_get_int64(setting)) != x)\
    return -1;\
}
#define CFG_WRT_UINT64(x,y) \
else if(write==1){\
  err = config_setting_set_int64(setting, x);\
  CHECK_CFG(y);\
}
#define CFG_GET_UINT64(x) \
else{\
  x = (unsigned long)config_setting_get_int64(setting);\
}
#define CFG_GET_INT(x) \
else{\
  x = config_setting_get_int(setting);\
}
#define CFG_WRT_INT(x,y) \
else if(write==1){\
  err = config_setting_set_int(setting, x);\
  CHECK_CFG(y);\
}
#define CFG_CHK_INT(x) \
if(check==1){\
  if(config_setting_get_int(setting) != x)\
    return -1;\
}
#define CFG_FULL_UINT64(x,y) \
CFG_ELIF(y){\
  if(config_setting_type(setting) != CONFIG_TYPE_INT64)	\
    return -1;\
  CFG_CHK_UINT64(x)\
  CFG_WRT_UINT64(x,y)\
  CFG_GET_UINT64(x)\
}
#define CFG_FULL_STR(x) \
CFG_ELIF(#x){\
  if(config_setting_type(setting) != CONFIG_TYPE_STRING)	\
    return -1;\
  CFG_CHK_STR(x)\
  CFG_WRT_STR(x)\
  CFG_GET_STR(x)\
}
#define CFG_FULL_INT(x,y)\
CFG_ELIF(y){\
  if(config_setting_type(setting) != CONFIG_TYPE_INT)	\
    return -1;\
  CFG_CHK_INT(x)\
  CFG_WRT_INT(x,y)\
  CFG_GET_INT(x)\
}
#define CFG_ADD_INT64(x)\
  do{\
setting = config_setting_add(root, #x, CONFIG_TYPE_INT64);\
CHECK_ERR_NONNULL(setting, "add "#x);\
  }while(0)
#define CFG_ADD_STR(x)\
  do{\
setting = config_setting_add(root, #x, CONFIG_TYPE_STRING);\
CHECK_ERR_NONNULL(setting, "add "#x);\
  }while(0)
#define CFG_ADD_INT(x)\
  do{\
setting = config_setting_add(root, #x, CONFIG_TYPE_INT);\
CHECK_ERR_NONNULL(setting, "add "#x);\
  }while(0)

//#define TIMERTYPE_GETTIMEOFDAY
#ifdef TIMERTYPE_GETTIMEOFDAY
#define TIMERTYPE struct timeval 
#define GETTIME(x) gettimeofday(&x,NULL)
//#define ZEROTIME(x) x.tv_sec =0;x.tv_usec=0;
#define SLEEP_NANOS(x) usleep((x.tv_usec))
#define COPYTIME(from,to) to.tv_sec = from.tv_sec;to.tv_usec=from.tv_usec
#define SETNANOS(x,y) x.tv_usec = (y)/1000
#define SETONE(x) x.tv_usec=1
#define GETNANOS(x) (x).tv_usec*1000
#else
#define TIMERTYPE struct timespec
#define GETTIME(x) clock_gettime(CLOCK_REALTIME, &x)
//#define ZEROTIME(x) x.tv_sec =0;x.tv_nsec=0;
#define SLEEP_NANOS(x) nanosleep(&x,NULL)
#define COPYTIME(from,to) to.tv_sec = from.tv_sec;to.tv_nsec=from.tv_nsec
#define SETNANOS(x,y) x.tv_nsec = (y)
#define GETNANOS(x) (x).tv_nsec
#define SETONE(x) x.tv_nsec=1
#endif
#define ZEROTIME(x) memset((void*)(&x),0,sizeof(TIMERTYPE))

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
#include "config.h"
#include <pthread.h>
#ifdef HAVE_LIBCONFIG_H
#include <libconfig.h>
#endif
#include <netdb.h> // struct hostent
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
/* This holds any entity, which can be set to either 	*/
/* A branches free or busy-list				*/
/* Cant decide if rather traverse the lists and remove	*/
/* father or keep it as is, to avoid traversing		*/
struct listed_entity
{
  struct listed_entity* child;
  struct listed_entity* father;
  int (*acquire)(void*,void*,unsigned long,unsigned long);
  int (*check)(void*, int);
  int (*close)(void*);
  int (*release)(void*);
  void* entity;
};
/* Holds all the listed_entits of a common type		*/
/* The idea is to manipulate and request entities from 	*/
/* this branch						*/
struct entity_list_branch
{
  struct listed_entity *freelist;
  struct listed_entity *busylist;
  struct listed_entity *loadedlist;
  pthread_mutex_t branchlock;
  /* Added here so the get_free caller can sleep	*/
  /* On non-free branch					*/
  pthread_cond_t busysignal;
};
/*
struct fileblocks
{
  int max_elements;
  int elements;
  INDEX_FILE_TYPE *files;
};
*/
/* Initial add */
void add_to_entlist(struct entity_list_branch* br, struct listed_entity* en);
/* Set this entity into the free to use list		*/
void set_free(struct entity_list_branch *br, struct listed_entity* en);
void set_loaded(struct entity_list_branch *br, struct listed_entity* en);
/* Get a free entity from the branch			*/
void* get_free(struct entity_list_branch *br, void * opt,unsigned long seq, unsigned long bufnum, int* acquire_result);
void* get_specific(struct entity_list_branch *br, void * opt,unsigned long seq, unsigned long bufnum, unsigned long id, int* acquire_result);
void* get_loaded(struct entity_list_branch *br, unsigned long seq);
void remove_from_branch(struct entity_list_branch *br, struct listed_entity *en, int mutex_free);
/* Set this entity as busy in this branch		*/
void set_busy(struct entity_list_branch *br, struct listed_entity* en);
void oper_to_all(struct entity_list_branch *be,int operation ,void* param);

//int fb_add_value(struct fileblocks* fb, unsigned long seq);

/* All the options for the main thread			*/
struct opt_s
{
  char *filename;

  /* Lock that spans over all threads. Used for tracking files	 	*/
  /* by sequence number							*/
  long unsigned int  cumul;
  /* Used in read to determine how many we actually found 		*/
  long unsigned int cumul_found;
  //pthread_mutex_t cumlock;
  char *device_name;
  char *cfgfile;
  char *logfile;
  int diskids;
  //unsigned long n_files;
  //struct fileblocks *fbs;
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
  int* fileholders;
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
  unsigned long total_packets;
#if(DAEMON)
  int running;
#endif
};
int write_cfgs_to_disks(struct opt_s *opt);
int read_full_cfg(struct opt_s *opt);
int parse_options(int argc, char **argv, struct opt_s* opt);
int clear_and_default(struct opt_s* opt);
int set_from_root(struct opt_s * opt, config_setting_t *root, int check, int write);
int remove_specific_from_fileholders(struct opt_s *opt, int id);
struct buffer_entity
{
  void * opt;
  //Functions for usage in modularized infrastructure
  /* TODO: This is getting bloated. Check what we're actually still using */
  int (*init)(struct opt_s* , struct buffer_entity*);
  int (*write)(struct buffer_entity*,int);
  void* (*get_writebuf)(struct buffer_entity *);
  /* Used to acquire element past the queue line */
  int (*acquire)(void * , void* , unsigned long, unsigned long);
  void* (*simple_get_writebuf)(struct buffer_entity *, int **);
  int* (*get_inc)(struct buffer_entity *);
  void (*set_ready)(struct buffer_entity*);
  void (*cancel_writebuf)(struct buffer_entity *);
  int (*wait)(struct buffer_entity *);
  int (*close)(struct buffer_entity*,void * );
  //int (*write_index_data)(struct buffer_entity*, void*, int);
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

  pthread_mutex_t *headlock;
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
  int (*check_files)(struct recording_entity*, void*);
  void (*get_stats)(void*, void*);

  int (*writecfg)(struct recording_entity *, void*);
  int (*readcfg)(struct recording_entity *, void*);

  int (*get_w_flags)();
  int (*get_r_flags)();
  int (*write_index_data)(const char*, long unsigned, void*, long unsigned);
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

int write_cfg(config_t *cfg, char* filename);
int read_cfg(config_t *cfg, char * filename);
int update_cfg(struct opt_s *opt, struct config_t * cfg);
int calculate_buffer_sizes(struct opt_s *opt);
int calculate_buffer_sizes_simple(struct opt_s * opt);
//Timerstuff
void specadd(struct timespec * to, struct timespec *from);
long nanodiff(TIMERTYPE * start, TIMERTYPE *end);
void nanoadd(TIMERTYPE * datime, unsigned long nanos_to_add);
void zeroandadd(TIMERTYPE *datime, unsigned long nanos_to_add);
int get_sec_diff(TIMERTYPE *timenow, TIMERTYPE* event);
void* vlbistreamer(void *opti);



#endif
