#ifndef STREAMER_H
#define STREAMER_H
/*
 * streamer.h -- Header file for single process manager for vlbistreamer
 *
 * Written by Tomi Salminen (tlsalmin@gmail.com)
 * Copyright 2012 Metsähovi Radio Observatory, Aalto University.
 * All rights reserved
 * This file is part of vlbi-streamer.
 *
 * vlbi-streamer is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * vlbi-streamer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with vlbi-streamer.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */
 	

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
#define B(x) (1l << x)
#define AIO_END_OF_FILE -323

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define BILLION 1E9l
#define MILLION 1E6l

#define KB 1024
#define BYTES_TO_MBITSPS(x)	(x*8)/(KB*KB)

#define MUTEX_FREE		1

/* What buf entity to use. Used by buf_type*/ 
#define LOCKER_WRITER		0x000000000000000f 
#define BUFFER_RINGBUF 		B(0)
#define WRITER_DUMMY 		B(1)
#define BUFFER_SIMPLE		B(2)

/* What HD writer to use. Used by rec_type*/
#define LOCKER_REC		0x0000000000000ff0
#define REC_AIO 		B(4)
#define REC_DUMMY 		B(5)
#define REC_DEF 		B(6)
#define REC_SPLICER 		B(7)

#define REC_WRITEV		B(8)
#define REC_SENDFILE		B(9)

/* How to capture packets. */
#define LOCKER_CAPTURE		0x0000000000fff000
#define CAPTURE_W_FANOUT 	B(12)
#define CAPTURE_W_UDPSTREAM 	B(13)
#define CAPTURE_W_DISK2FILE	B(14)
#define CAPTURE_W_DUMMY		B(15)

#define CAPTURE_W_TCPSTREAM	B(16)
#define CAPTURE_W_TCPSPLICE	B(17)
#define CAPTURE_W_LOCALSOCKET	B(18)
#define CAPTURE_W_MULTISTREAM	B(19)

#define WILL_GIVE_SOCKET	B(24)
#define VERBOSE			B(25)
#define MOUNTPOINT_VERBOSE	B(26)
#define SIMPLE_BUFFER		B(27)

#define USE_RX_RING		B(28)
#define GET_A_FILENAME_AS_ARG	B(29)
#define CAN_STRIP_BYTES		B(30)
#define WAIT_START_ON_METADATA	B(31)

/* Set only if the writer can actually strip bytes from packets	*/

/* DATATYPE-stuff is now at datatypes_common	*/
/* Keep 32-39 empty!				*/

#define FORCE_SOCKET_REACQUIRE	B(40)
#define USE_TCP_SOCKET		B(41)
#define CONNECT_BEFORE_SENDING	B(42)
#define WRITE_TO_SINGLE_FILE	B(43)

#define SO_REUSEIT		B(44)
#define WILL_GET_SOCKET		B(45)
#define ASYNC_WRITE		B(46)
#define READMODE		B(47)

#define USE_HUGEPAGE		B(48)
#define USE_LARGEST_TRANSAC	B(49)

#define MEG			B(20)
#define GIG			B(30)

#define STATUS_NOT_STARTED	B(0)
#define STATUS_RUNNING		B(1)
#define	STATUS_STOPPED		B(2)
#define STATUS_FINISHED		B(3)
#define STATUS_ERROR		B(4)
#define STATUS_CANCELLED	B(5)

#define RECSTATUS_OK		B(0)
#define RECSTATUS_FULL		B(1)
#define RECSTATUS_ERROR		B(2)

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

#define MBITS_PER_DRIVE	750 
#define MBYTES_PER_DRIVE (MBITS_PER_DRIVE/8)
#define TOTAL_MAX_DRIVES_IN_USE 20


#define INITIAL_N_FILES B(7)

#define SIMPLE_BUFCACL_SINGLEFILE
#ifdef SIMPLE_BUFCACL_SINGLEFILE
#define CALC_BUF_SIZE(x) calculate_buffer_sizes_singlefilesize(x)
#elif SIMPLE_BUFCACL
#define CALC_BUF_SIZE(x) calculate_buffer_sizes_simple(x)
#else
define CALC_BUF_SIZE(x) calculate_buffer_sizes(x)
#endif

#define MIN_MEM_GIG 4l
#define MAX_MEM_GIG 12l
  /* TODO query this */
#define BLOCK_ALIGN 4096
  /* Default lenght of index following file as in <filename>.[0-9]8 */
#define INDEXING_LENGTH 8

#define HAVE_ASSERT 1
#define ASSERT(x) do{if(HAVE_ASSERT){assert(x);}}while(0)

  /*
#define MAX_PRIO_FOR_PTHREAD 1
#define RBUF_PRIO	3
#define RECEIVE_THREAD_PRIO 1
#define SEND_THREAD_PRIO 2
#define MIN_PRIO_FOR_PTHREAD 4
*/
#define MIN_PRIO_FOR_UNIMPORTANT	10

  /* Default packet size */
#define DEF_BUF_ELEM_SIZE 8192
  //#define BUF_ELEM_SIZE 32768
#define MAX_OPEN_FILES 48
  //Magic number TODO: Refactor so we won't need this
#define WRITE_COMPLETE_DONT_SLEEP 1337
  /* The length of our indices. A week at 10Gb/s is 99090432000 packets for one thread*/
#define INDEX_FILE_TYPE unsigned long
  /* TODO: Make the definition below work where used */
#define INDEX_FILE_PRINT lu
  /* Moving the rbuf-stuff to its own thread */
#define SPLIT_RBUF_AND_IO_TO_THREAD

#define HD_MIN_WRITE_SIZE 16777216
  //Default file size as 500MB
#define FILE_SPLIT_TO_BLOCKS B(29)l
#define MIN(x,y) (x < y ? x : y)

#define WRITEND_USES_DIRECTIO(x) ((x)->optbits &(REC_AIO|REC_DEF))
#define WRITEND_DOESNT_SUPPORTS_LIMIT(x) ((x)->optbits &(REC_WRITEV))
#define WRITEND_WANTS_PAGESIZE(x) ((x)->optbits &(REC_SPLICER))

#define CALC_BUFSIZE_FROM_OPT(opt) ((opt)->buf_num_elems*((opt)->packet_size-(opt)->offset))
#define CALC_BUFSIZE_FROM_OPT_NOOFFSET(opt) ((opt)->buf_num_elems*((opt)->packet_size))

#include <pthread.h>
#include <config.h>
#ifdef HAVE_LIBCONFIG_H
#include <libconfig.h>
#endif
#include <netdb.h> // struct hostent
#include "config.h"
#include "resourcetree.h"
#include "logging.h"
#include "timer.h"

extern FILE* logfile; 
struct stats
{
  unsigned long total_packets;
  unsigned long total_bytes;
  unsigned long total_written;
  unsigned long incomplete;
  unsigned long dropped;
  int progress;
  //unsigned long files_exchanged;
  //Cheating here to keep infra consistent
  //int * packet_index;
};
struct rxring_request{
  long unsigned* id;
  int* bufnum;
};
/* All the options for the main thread			*/
struct opt_s
{
  char *filename;
  /* Lock that spans over all threads. Used for tracking files	 	*/
  /* by sequence number							*/
  long unsigned cumul;
  /* Used in read to determine how many we actually found 		*/
  long unsigned cumul_found;
  long unsigned last_packet;
  //pthread_mutex_t cumlock;
  char *address_to_bind_to;
  char *cfgfile;
  int diskids;
  struct file_index * fi;
  unsigned long optbits;
  int root_pid;
  int hd_failures;
  unsigned long time;
  int port;
  unsigned long minmem;
  unsigned long maxmem;
  struct entity_list_branch *membranch;
  struct entity_list_branch *diskbranch;
  int n_threads;
  int n_drives;
  int bufculum;
  int rate;
  void * first_packet;
  void * resqut;

  int localsocket;

  /* Used to skip writing of some headers */
  int offset;
  /* Used when reading a stripped recording. File size will be same	*/
  /* but it will have less elements					*/
  int offset_onwrite;
  char * disk2fileoutput;
  /* Used if RX-ring for receive */
  void* buffer;
#ifdef HAVE_LIBCONFIG_H
  config_t cfg;
#endif
  unsigned long do_w_stuff_every;
#ifdef HAVE_RATELIMITER
  int wait_nanoseconds;
  /* TODO: move this to spec_ops or similar */
  TIMERTYPE wait_last_sent;
#endif
  TIMERTYPE starting_time;
  TIMERTYPE endtime;
  unsigned long filesize;

  /* Bloat TODO Find alternative place for this */
  INDEX_FILE_TYPE packet_size;
  unsigned int buf_num_elems;
  int buf_division;
  //These two are a bit silly. Should be moved to use as a parameter
  char * hostname;
#if(DAEMON)
  void (*get_stats)(void*, void*);
  long unsigned bytes_exchanged;
#endif
  unsigned long total_packets;
  pthread_t *rbuf_pthreads;
  struct buffer_entity * bes;
  struct recording_entity *recs;
#ifdef TUNE_AFFINITY
  cpu_set_t cpuset;
#endif
  /* Used for writing to a single continuous file	*/
  int singlefile_fd;
  long unsigned next_fd_id_to_write;
  /* Used to sleep while waiting for write to finish	*/
  /* on previous file size chunk			*/
  pthread_mutex_t * writequeue;
  pthread_cond_t * writequeue_signal;
  
  /* For TCP transfers. Expects stream_multiply accepts/connects on	*/
  /* both ends and divides the transfer to individual streams		*/
  int stream_multiply;

  int status;
#if(PROTECT_STATUS_W_RWLOCK)
  pthread_rwlock_t statuslock;
#endif
  struct streamer_entity * streamer_ent;
};
int parse_options(int argc, char **argv, struct opt_s* opt);
int clear_and_default(struct opt_s* opt, int create_cfg);
int clear_pointers(struct opt_s* opt);

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
  void* (*simple_get_writebuf)(struct buffer_entity *,unsigned long **);
  //int* (*get_inc)(struct buffer_entity *);
  void (*set_ready)(struct buffer_entity*,int);
  void (*set_ready_and_signal)(struct buffer_entity*,int);
  void (*cancel_writebuf)(struct buffer_entity *);
  //int (*wait)(struct buffer_entity *);
  int (*close)(struct buffer_entity*,void * );
  void* (*write_loop)(void *);
  void (*stop)(struct buffer_entity*);
  void (*init_mutex)(struct buffer_entity *, void*,void*);
  int (*get_shmid)(struct buffer_entity*);
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
  int (*is_blocked)(struct buffer_entity*);
#endif
  struct recording_entity * recer;
  struct listed_entity * self;
  void * buffer;
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
  off_t (*get_filesize)(void*);

  int (*writecfg)(struct recording_entity *, void*);
  int (*readcfg)(struct recording_entity *, void*);

  int (*get_w_flags)();
  int (*handle_error)(struct recording_entity *, int);
  int (*get_r_flags)();
  void (*setshmid)(void*, int, void*);
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
  int (*init)(struct opt_s *, struct streamer_entity *);
  void* (*start)(void*);
  int (*close)(struct streamer_entity*,void*);
  void (*stop)(struct streamer_entity *);
  void (*close_socket)(struct streamer_entity *se);
  /* Added to get periodic stats */
  void (*get_stats)(void*, void*);
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
  int (*is_blocked)(struct streamer_entity *);
#endif
  //int (*is_running)(struct streamer_entity *se);
  unsigned long (*get_max_packets)(struct streamer_entity *);
  /* TODO: Refactor streamer to use the same syntax as buffer and writer */
  struct buffer_entity *be;
  struct listed_entity *rbuf;
  //struct entity_list_branch *membranch;
};

struct scheduled_event{
  struct opt_s * opt;
  //struct scheduled_event* next;
  struct stats* stats;
  char * idstring;
  int socketnumber;
  void (*shutdown_thread)(struct opt_s*);
  pthread_t pt;
  int found;
};
/* Just to cut down on number of variables passed to functions		*/
struct schedule{
  struct entity_list_branch br;
  //listed_entity* scheduled_head;
  //listed_entity* running_head;
  struct opt_s * default_opt;
  TIMERTYPE lasttick;
  int n_scheduled;
  int n_running;
};
int ret_zero_if_stillshouldrun(void * opt);

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
void arrange_by_id(struct opt_s* opt);
//Timerstuff
int close_streamer(struct opt_s *opt);
int init_branches(struct opt_s *opt);
void shutdown_thread(struct opt_s *opt);
int prep_priority(struct opt_s * opt, int priority);
int prep_streamer(struct opt_s * opt);
#if(DAEMON)
int print_midstats(struct schedule* sched, struct stats* old_stats); 
#endif
#if(DAEMON)
void* vlbistreamer(void *opti);
#endif
int iden_from_opt(struct opt_s *opt, void* val1, void* val2, int iden_type);
void print_stats(struct stats *stats, struct opt_s * opts);
#if(PPRIORITY)
int minimize_priority();
#endif
int get_status_from_opt(struct opt_s* opt);
void set_status_for_opt(struct opt_s* opt, int status);
#endif
