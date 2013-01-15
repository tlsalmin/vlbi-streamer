/*
 * streamer.c -- Single process manager for vlbistreamer
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
#include <time.h>
#include <sched.h>
#include "config.h"
#include "streamer.h"
#include "fanout.h"
#include "udp_stream.h"
//#include "ringbuf.h"
#ifdef HAVE_LIBAIO
#include "aiowriter.h"
#endif
#include "common_wrt.h"
#include "defwriter.h"
//#include "sendfile_streamer.h"
#include "splicewriter.h"
#include "writev_writer.h"
#include "disk2file.h"
#include "resourcetree.h"
#include "confighelper.h"
#include "simplebuffer.h"
#include "dummywriter.h"
#include "dummy_stream.h"
#define IF_DUPLICATE_CFG_ONLY_UPDATE
/* from http://stackoverflow.com/questions/1076714/max-length-for-client-ip-address */
/* added one for null char */
#define IP_LENGTH 46
/* Segfaults if pthread_joins done at exit. Tried to debug this 	*/
/* for days but no solution						*/
//#define UGLY_FIX_ON_RBUFTHREAD_EXIT
//TODO: Search these
#if(DAEMON)
#define STREAMER_CHECK_NONNULL(val,mes) do{if(val==NULL){perror(mes);E(mes);pthread_exit(NULL);}else{D(mes);}}while(0)
#else
#define STREAMER_CHECK_NONNULL(val,mes) do{if(val==NULL){perror(mes);E(mes);return -1;}else{D(mes);}}while(0)
#endif
#if(DAEMON)
/* NOTE: Dont use these in dangling if or else */
#define STREAMER_EXIT opt->status = STATUS_FINISHED; pthread_exit(NULL)
#define STREAMER_ERROR_EXIT opt->status = STATUS_ERROR; pthread_exit(NULL)
#else
#define STREAMER_EXIT return 0
#define STREAMER_ERROR_EXIT return -1
#endif

#if(PPRIORITY)
#define FREE_AND_ERROREXIT if(opt->device_name != NULL){free(opt->device_name);} config_destroy(&(opt->cfg)); free(opt->membranch); free(opt->diskbranch); pthread_attr_destroy(&pta);exit(-1);
#else
#define FREE_AND_ERROREXIT if(opt->device_name != NULL){free(opt->device_name);} config_destroy(&(opt->cfg)); free(opt->membranch); free(opt->diskbranch); exit(-1);
#endif
/* This should be more configurable */
extern char *optarg;
extern int optind, optopt;

FILE* logfile;

void udpstreamer_stats(void* opts, void* statsi){
  struct opt_s* opt = (struct opt_s*) opts;
  struct stats* stats = (struct stats*)statsi;
  opt->streamer_ent->get_stats(opt->streamer_ent->opt, stats);
  //stats->total_written += opt->bytes_exchanged;
}
int calculate_buffer_sizes(struct opt_s *opt){
  /* Calc how many elementes we get into the buffer to fill the minimun */
  /* amount of memory we want to use					*/

  /* Magic is the n of blocks we wan't to divide the ringbuffer to	*/
  opt->buf_division = 8;
  //unsigned long bufsize;// = opt.packet_size;
  int found = 0;

  int extra= 0;
  if(opt->optbits & USE_RX_RING){
    while((opt->packet_size %16)!= 0){
      if(opt->optbits  &READMODE)
	E("Shouldn't need this in sending with RX-ring!");
      opt->packet_size++;
      extra++;
    }
    D("While using RX-ring we need to reserve %d extra bytes per buffer element",, extra);
  }

  /* TODO: do_w_stuff gets warped  from MB to num of elems*/
  LOG("STREAMER: Calculating total buffer size between "
      "%lu GB to %luGB,"
      " size %lu packets, "
      "Doing maximum %luMB size writes\n"
      ,opt->minmem, opt->maxmem, opt->packet_size, opt->do_w_stuff_every/MEG);
  /* Set do_w_stuff to minimum wanted */
  /* First set do_w_stuff to be packet aligned */
  unsigned long temp = opt->do_w_stuff_every/opt->packet_size;
  LOG("%lu\n",temp);
  opt->do_w_stuff_every = temp*(opt->packet_size);

  /* Increase block division to fill min amount of memory */
  while((opt->do_w_stuff_every)*opt->buf_division*(opt->n_threads) < (opt->minmem)*GIG){
    opt->buf_division++;
  }
  /* Store for later use if proper size not found with current opt->buf_division */
  temp = opt->do_w_stuff_every;
  while((found == 0) && (opt->buf_division > 0)){
    /* Increase buffer size until its BLOCK_ALIGNed */
    while((opt->do_w_stuff_every)*opt->buf_division*(opt->n_threads) < (opt->maxmem)*GIG){
      if(opt->do_w_stuff_every % BLOCK_ALIGN == 0){
	found=1;
	opt->buf_num_elems = (opt->do_w_stuff_every*opt->buf_division)/opt->packet_size;
	//opt->do_w_stuff_every = opt->do_w_stuff_every/opt->packet_size;
	break;
      }
      opt->do_w_stuff_every+=opt->packet_size;
    }
    if(found == 0){
      opt->do_w_stuff_every = temp;
      opt->buf_division--;
    }
  }
  if(found ==0){
    LOGERR("STREAMER: Didnt find Alignment"
	"%lu GB to %luGB"
	", Each buffer having %lu bytes"
	", Writing in %lu size blocks"
	", %d Blocks per buffer"
	", Elements in buffer %d\n"
	,opt->minmem, opt->maxmem, opt->packet_size*(opt->buf_num_elems), opt->do_w_stuff_every,opt->buf_division ,opt->buf_num_elems);
    //LOG("STREAMER: Didnt find alignment for %lu on %d threads, with w_every %lu\n", opt->packet_size,opt->n_threads, (opt->packet_size*(opt->buf_num_elems))/opt->buf_division);
    return -1;
  }
  else{
    if(opt->optbits & USE_RX_RING){
      D("The 16 aligned restriction of RX-ring resulted in %ld MB larger memory use",, extra*opt->buf_num_elems*opt->n_threads/MEG);
    }

    /*
       long filesztemp =0;
       while(filesztemp < opt.filesize)
       filesztemp+=opt.do_w_stuff_every;
       opt.filesize= filesztemp;
       */
    //opt.filesize = opt->buf_num_elems*(opt->packet_size);

    LOG("STREAMER: Alignment found between "
	"%lu GB to %luGB"
	", Each buffer having %lu MB"
	", Writing in %lu MB size blocks"
	", Elements in buffer %d"
	", Total used memory: %luMB\n"
	,opt->minmem, opt->maxmem, (opt->packet_size*(opt->buf_num_elems))/MEG, (opt->do_w_stuff_every)/MEG, opt->buf_num_elems, (opt->buf_num_elems*opt->packet_size*opt->n_threads)/MEG);
    //LOG("STREAMER: Alignment found for %lu size packet with %d threads at %lu with ringbuf in %lu blocks. hd write size as %lu\n", opt->packet_size,opt->n_threads ,opt->buf_num_elems*(opt->packet_size),opt->buf_division, (opt->buf_num_elems*opt->packet_size)/opt->buf_division);
    return 0;
  }
}
int rxring_packetadjustment(struct opt_s * opt){
  int extra=0;
  while((opt->packet_size %16)!= 0){
    if(opt->optbits  &READMODE)
      E("Shouldn't need this in sending with RX-ring!");
    opt->packet_size++;
    extra++;
  }
  D("While using RX-ring we need to reserve %d extra bytes per buffer element",, extra);
  return extra;
}
int calculate_buffer_sizes_simple(struct opt_s * opt){ 
  /* A very simple buffer size calculator that fixes the filesizes to 	*/
  /* constants according to the packet size. We try to keep the		*/
  /*filesize between 256 and 512 and must keep it packet- and 		*/
  /* blockaligned at all times						*/
  //int extra= 0;
  if(opt->optbits & USE_RX_RING){
    rxring_packetadjustment(opt);
  }
  opt->buf_division = B(3);
  //while(opt->packet_size*BLOCK_ALIGN*opt->buf_division >= MAXFILESIZE*MEG)
    //opt->buf_division  >>= 1;
  while(opt->packet_size*BLOCK_ALIGN*opt->buf_division <= opt->filesize*MEG)
    opt->buf_division  <<= 1;
  opt->buf_num_elems = (BLOCK_ALIGN*opt->buf_division);
  opt->do_w_stuff_every = (BLOCK_ALIGN*opt->packet_size);

  opt->n_threads = 1;
  while(opt->n_threads*opt->packet_size*opt->buf_num_elems < opt->maxmem*GIG)
    opt->n_threads++;
  opt->n_threads -=1;

  return 0;
}
int calculate_buffer_sizes_singlefilesize(struct opt_s * opt){ 
//int extra= 0;
  if(opt->optbits & USE_RX_RING){
    rxring_packetadjustment(opt);
  }
  opt->buf_num_elems = (opt->filesize)/opt->packet_size;
  opt->n_threads = (opt->maxmem*GIG)/opt->filesize;
  return 0;

}
/*
 * Adapted from http://coding.debuntu.org/c-linux-socket-programming-tcp-simple-http-client
 */
/*
   int resolve_host(char *host, struct in_addr * ia){
   int err=0;
   return err;
   }
   */

/*
 * Stuff stolen from lindis sendfileudp
 */
static void usage(char *binary){
  LOGERR(
      "usage: %s [OPTIONS]... name (time to receive / host to send to)\n"
      "-A MAXMEM	Use maximum MAXMEM amount of memory for ringbuffers(default 12GB)\n"
#ifdef HAVE_RATELIMITER
      "-a MYY		Wait MYY microseconds between packet sends\n"
#endif
      "-t {fanout|udpstream|sendfile|TODO	Capture type(Default: udpstream)(sendfile is a prototype not yet in kernel)(fanout doesn't write to disk. Poor performance)\n"
      "-c CFGFILE	Load config from cfg-file CFGFILE\n"
      //"-a {lb|hash}	Fanout type(Default: lb)\n"
      "-d DRIVES	Number of drives(Default: 1)\n"
      "-i INTERFACE	Which interface to bind to(Not required)\n"
      //"-I MINMEM	Use at least MINMEM amount of memory for ringbuffers(default 4GB)\n"
      "-m {s|r}	Send or Receive the data(Default: receive)\n"
      "-n IP	        Resend packet immediately to IP \n"
      "-p SIZE		Set buffer element size to SIZE(Needs to be aligned with sent packet size)\n"
      "-q DATATYPE	Receive DATATYPE type of data and resequence (DATATYPE: vdif, mark5b,udpmon,none)\n"
      "-r RATE		Expected network rate in Mb/s. \n"
      "-s SOCKET	Socket number(Default: 2222)\n"
#if(HAVE_HUGEPAGES)
      "-u 		Use hugepages\n"
#endif
      "-v 		Verbose. Print stats on all transfers\n"
      //"-V 		Verbose. Print stats on individual mountpoint transfers\n"
      //"-W WRITEEVERY	Try to do HD-writes every WRITEEVERY MB(default 16MB)\n"
      "-w {"
#ifdef HAVE_LIBAIO
      "aio|"
#endif
      "def|splice|dummy}	Choose writer to use(Default: def)\n"
      "-x 		Use an mmap rxring for receiving\n"
      ,binary);
}
/* Why don't I just memset? */
void init_stats(struct stats *stats){
  memset(stats, 0,sizeof(struct stats));
  /*
  stats->total_bytes = 0;
  stats->incomplete = 0;
  stats->total_written = 0;
  stats->total_packets = 0;
  stats->dropped = 0;
  */
}
void neg_stats(struct stats* st1, struct stats* st2){
  /* We sometimes get a situation, where the previous val is larger 	*/
  /* than the new value. This shouldn't happen! For now I'll just add	*/
  /* an ugly hack here. TODO: Solve					*/
  /* NOTE: This doesn't affect the final stats				*/
#ifdef UGLY_HACKS_ON_STATS
  if(st1->total_bytes < st2->total_bytes)
    st1->total_bytes =0 ;
  else
#endif
    st1->total_bytes -= st2->total_bytes;
  st1->incomplete -= st2->incomplete;
#ifdef UGLY_HACKS_ON_STATS
  if(st1->total_written < st2->total_written)
    st1->total_written =0 ;
  else
#endif
    st1->total_written -= st2->total_written;
#ifdef UGLY_HACKS_ON_STATS
  if(st1->total_packets < st2->total_packets)
    st1->total_packets =0 ;
  else
#endif
    st1->total_packets -= st2->total_packets;
  st1->dropped -= st2->dropped;
}
void add_stats(struct stats* st1, struct stats* st2){
  st1->total_bytes += st2->total_bytes;
  st1->incomplete += st2->incomplete;
  st1->total_written += st2->total_written;
  st1->dropped += st2->dropped;
}
void print_intermediate_stats(struct stats *stats){
  LOG("Net Send/Receive completed: \t%luMb/s\n"
      "HD Read/write completed \t%luMb/s\n"
      "Dropped %lu\tIncomplete %lu\n"
      ,BYTES_TO_MBITSPS(stats->total_bytes), BYTES_TO_MBITSPS(stats->total_written), stats->dropped, stats->incomplete);
}
void print_stats(struct stats *stats, struct opt_s * opts){
  if(opts->optbits & READMODE){
    LOG("Stats for %s \n"
	"Packets: %lu\n"
	"Bytes: %lu\n"
	"Read: %lu\n"
	"Sendtime: %lus\n"
	"Files: %lu\n"
	"HD-failures: %d\n"
	//"Net send Speed: %fMb/s\n"
	//"HD read Speed: %fMb/s\n"
	,opts->filename, stats->total_packets, stats->total_bytes, stats->total_written,opts->time, *opts->cumul,opts->hd_failures);//, (((float)stats->total_bytes)*(float)8)/((float)1024*(float)1024*opts->time), (stats->total_written*8)/(1024*1024*opts->time));
  }
  else{
    if(opts->time == 0)
      E("SendTime is 0. Something went wrong");
    else
      LOG("Stats for %s \n"
	  "Packets: %lu\n"
	  "Bytes: %lu\n"
	  "Dropped: %lu\n"
	  "Incomplete: %lu\n"
	  "Written: %lu\n"
	  "Recvtime: %lu\n"
	  "Files: %lu\n"
	  "HD-failures: %d\n"
	  "Net receive Speed: %luMb/s\n"
	  "HD write Speed: %luMb/s\n"
	  ,opts->filename, stats->total_packets, stats->total_bytes, stats->dropped, stats->incomplete, stats->total_written,opts->time, *opts->cumul,opts->hd_failures, (stats->total_bytes*8)/(1024*1024*opts->time), (stats->total_written*8)/(1024*1024*opts->time));
  }
}
/* Defensive stuff to check we're not copying stuff from default	*/
int clear_pointers(struct opt_s* opt){
  opt->filename = NULL;
  opt->device_name = NULL;
  opt->hostname = NULL;
  opt->cfgfile = NULL;
  opt->cumul = NULL;
  opt->disk2fileoutput = NULL;
  return 0;
}
int clear_and_default(struct opt_s* opt, int create_cfg){
  //int i;
  memset(opt, 0, sizeof(struct opt_s));
  opt->filename = NULL;
  opt->device_name = NULL;
  opt->cfgfile = NULL;
  opt->hostname = NULL;
  opt->streamer_ent = NULL;
  /*
  for(i=0;i<MAX_OPEN_FILES;i++){
    opt->filenames[i] = NULL;
  }
  */

  opt->diskids = 0;
  opt->hd_failures = 0;
  opt->filesize = FILESIZE;
#if(DAEMON)
  opt->status = STATUS_NOT_STARTED;
#endif

  if(create_cfg == 1)
    config_init(&(opt->cfg));

  /* Opts using optbits */
  //opt->capture_type = CAPTURE_W_FANOUT;
  opt->optbits |= CAPTURE_W_UDPSTREAM;
  opt->do_w_stuff_every = HD_MIN_WRITE_SIZE;
  //opt->fanout_type = PACKET_FANOUT_LB;
  //opt->optbits |= PACKET_FANOUT_LB;
  opt->root_pid = getpid();
  opt->port = 2222;
  opt->n_threads = 0;
  opt->n_drives = 1;
  opt->packet_size = DEF_BUF_ELEM_SIZE;
  opt->optbits |= DATATYPE_UNKNOWN;
  opt->cumul_found = 0;
  opt->last_packet = 0;

  //opt->optbits |=USE_RX_RING;
  //TODO: Add option for choosing backend
  //opt->buf_type = BUFFER_RINGBUF;
  opt->optbits |= BUFFER_SIMPLE;
  /* Calculated automatically when aligment is calculated */
  //opt->filesize = FILE_SPLIT_TO_BLOCKS;
  //opt->rec_type= REC_DEF;
  opt->optbits |= REC_DEF;
  opt->taken_rpoints = 0;
  opt->rate = 10000;
  opt->minmem = MIN_MEM_GIG;
  opt->maxmem = MAX_MEM_GIG;
  //opt->handle = 0;
  //opt->read = 0;
  opt->tid = 0;
  //opt->async = 0;
  //opt->optbits = 0xff000000;
  opt->optbits |= SIMPLE_BUFFER;
  opt->socket = 0;
  memset(&opt->start_time, 0,sizeof(TIMERTYPE));
  memset(&opt->wait_last_sent, 0,sizeof(TIMERTYPE));

  //opt->cumul = NULL;
#if(!DAEMON)
  opt->optbits |= GET_A_FILENAME_AS_ARG;
  opt->cumul = (long unsigned *)malloc(sizeof(long unsigned));
  opt->total_packets = (long unsigned *)malloc(sizeof(long unsigned));
#endif


  return 0;
}
int parse_options(int argc, char **argv, struct opt_s* opt){
  int ret;
  /*
#ifdef USE_FOR_DISK2FILE
  while((ret = getopt(argc, argv, "d:i:t:s:n:m:w:p:q:ur:a:vVI:A:W:xc:o:"))!= -1){
#else
*/
  while((ret = getopt(argc, argv, "d:i:t:s:n:m:w:p:q:ur:a:vVI:A:W:xc:"))!= -1){
//#endif
    switch (ret){
      case 'i':
	opt->device_name = strdup(optarg);
	break;
      case 'c':
	opt->cfgfile = (char*)malloc(sizeof(char)*FILENAME_MAX);
	//CHECK_ERR_NONNULL(opt->cfgfile, "Cfgfile malloc");
	//opt->cfgfile = strdup(optarg);
	sprintf(opt->cfgfile, "%s", optarg);
//#if(!DAEMON)
	LOG("Path for cfgfile specified. All command line options before this argument might be ignored\n");
	ret = read_full_cfg(opt);
	if(ret != 0){
	  E("Error parsing cfg file. Exiting");
	  free(opt->cfgfile);
	  return -1;
	}
	//free(opt->cfgfile);
	//opt->cfgfile = NULL;
//#endif
	break;
      case 'v':
	opt->optbits |= VERBOSE;
	break;
      case 'd':
	opt->n_drives = atoi(optarg);
	break;
#if(DAEMON)
      case 'e':
	opt->start_time.tv_sec = atoi(optarg);
	break;
#endif
      case 'I':
	opt->minmem = atoi(optarg);
	break;
      case 'x':
	opt->optbits |= USE_RX_RING;
	break;
      case 'W':
	opt->do_w_stuff_every = atoi(optarg)*MEG;
	break;
      case 'A':
	opt->maxmem = atoi(optarg);
	break;
      case 'V':
	opt->optbits |= MOUNTPOINT_VERBOSE;
	break;
      case 't':
	opt->optbits &= ~LOCKER_CAPTURE;
	if (!strcmp(optarg, "fanout")){
	  //opt->capture_type = CAPTURE_W_FANOUT;
	  opt->optbits |= CAPTURE_W_FANOUT;
	}
	else if (!strcmp(optarg, "udpstream")){
	  //opt->capture_type = CAPTURE_W_UDPSTREAM;
	  opt->optbits |= CAPTURE_W_UDPSTREAM;
	}
	else if (!strcmp(optarg, "sendfile")){
	  //opt->capture_type = CAPTURE_W_SPLICER;
	  opt->optbits |= CAPTURE_W_SPLICER;
	}
	else if (!strcmp(optarg, "dummy")){
	  //opt->capture_type = CAPTURE_W_SPLICER;
	  opt->optbits |= CAPTURE_W_DUMMY;
	}
	else {
	  LOGERR("Unknown packet capture type [%s]\n", optarg);
	  usage(argv[0]);
	  return -1;
	}
	break;
	/* Fanout choosing removed and set to default LB since
	 * Implementation not that feasible anyway
	 case 'a':
	 opt->optbits &= ~LOCKER_FANOUT;
	 if (!strcmp(optarg, "hash")){
	//opt->fanout_type = PACKET_FANOUT_HASH;
	opt->optbits |= PACKET_FANOUT_HASH;
	}
	else if (!strcmp(optarg, "lb")){
	//opt->fanout_type = PACKET_FANOUT_LB;
	opt->optbits |= PACKET_FANOUT_LB;
	}
	else {
	LOGERR("Unknown fanout type [%s]\n", optarg);
	usage(argv[0]);
	exit(1);
	}
	break;
	*/
      case 'a':
#ifdef HAVE_RATELIMITER
	//opt->optbits |= WAIT_BETWEEN;
	opt->wait_nanoseconds = atoi(optarg)*1000;
	ZEROTIME(opt->wait_last_sent);
#else
	LOGERR("STREAMER: Rate limiter not compiled\n");
#endif
	break;
      case 'r':
	opt->rate = atoi(optarg);
	break;
      case 's':
	opt->port = atoi(optarg);
	break;
      case 'p':
	opt->packet_size = atoi(optarg);
	break;
      case 'u':
#if(HAVE_HUGEPAGES)
	opt->optbits |= USE_HUGEPAGE;
#endif
	break;
      case 'n':
	/* Commandeering this option */
	opt->hostname = (char*)malloc(sizeof(char)*IP_LENGTH);
	if(strcpy(opt->hostname, optarg) == NULL){
	  E("strcpy hostname");
	  return -1;
	}
	//opt->n_threads = atoi(optarg);
	break;
      case 'q':
	opt->optbits &= ~LOCKER_DATATYPE;
	if (!strcmp(optarg, "vdif")){
	  D("Datatype as VDIF");
	  opt->optbits |= DATATYPE_VDIF;
	}
	else if(!strcmp(optarg, "mark5b")){
	  D("Datatype as Mark5");
	  opt->optbits |= DATATYPE_MARK5B;
	}
	else if(!strcmp(optarg, "udpmon")){
	  D("Datatype as UDPMON");
	  opt->optbits |= DATATYPE_UDPMON;
	}
	else if(!strcmp(optarg, "none")){
	  D("Datatype as none");
	  opt->optbits |= DATATYPE_UNKNOWN;
	}
	else{
	  E("Unknown datatype %s",, optarg);
	  opt->optbits |= DATATYPE_UNKNOWN;
	}
	break;
	/*
#ifdef USE_FOR_DISK2FILE
      case 'o':
	opt->out_filename = (char*)malloc(sizeof(char)*FILENAME_MAX);
	CHECK_ERR_NONNULL(opt->filename, "filename malloc");
	if(strcpy(opt->filename, argv[0]) == NULL){
	  E("strcpy filename");
	  return -1;
	}
	break;
#endif
*/
      case 'm':
	if (!strcmp(optarg, "r")){
	  opt->optbits &= ~READMODE;
	  //opt->read = 0;
	}
	else if (!strcmp(optarg, "s")){
	  //opt->read = 1;
	  opt->optbits |= READMODE;
	}
	else {
	  LOGERR("Unknown mode type [%s]\n", optarg);
	  usage(argv[0]);
	  exit(1);
	}
	break;
      case 'w':
	opt->optbits &= ~LOCKER_REC;
	if (!strcmp(optarg, "def")){
	  /*
	     opt->rec_type = REC_DEF;
	     opt->async = 0;
	     */
	  opt->optbits |= REC_DEF;
	  opt->optbits &= ~ASYNC_WRITE;
	}
#ifdef HAVE_LIBAIO
	else if (!strcmp(optarg, "aio")){
	  /*
	     opt->rec_type = REC_AIO;
	     opt->async = 1;
	     */
	  opt->optbits |= REC_AIO|ASYNC_WRITE;
	}
#endif
	else if (!strcmp(optarg, "splice")){
	  /*
	     opt->rec_type = REC_SPLICER;
	     opt->async = 0;
	     */
	  opt->optbits |= REC_SPLICER;
	  opt->optbits &= ~ASYNC_WRITE;
	}
	else if (!strcmp(optarg, "dummy")){
	  /*
	     opt->rec_type = REC_DUMMY;
	     opt->buf_type = WRITER_DUMMY;
	     */
	  opt->optbits &= ~LOCKER_WRITER;
	  opt->optbits |= REC_DUMMY|WRITER_DUMMY;
	  opt->optbits &= ~ASYNC_WRITE;
	}
	else {
	  LOGERR("Unknown mode type [%s]\n", optarg);
	  usage(argv[0]);
	  //exit(1);
	  return -1;
	}
	break;
      default:
	usage(argv[0]);
	exit(1);
    }
  }
#if(!DAEMON)
  if(argc -optind != 2){
    usage(argv[0]);
    exit(1);
  }
#endif
  argv +=optind;
  argc -=optind;

  /* TODO: Enable giving a custom cfg-file on invocation */
#if(!DAEMON)
  if(opt->cfgfile!=NULL){
  }
#endif //DAEMON

  /* If we're using rx-ring, then set the packet size to +TPACKET_HDRLEN */
  /*
     if(opt->optbits & USE_RX_RING)
     opt->packet_size += TPACKET_HDRLEN;
     */

  /* If n_threads isn't set, set it to n_drives +2 	*/
  /* Not used. Maxmem limits this instead		*/
  /*
     if(opt->n_threads == 0)
     opt->n_threads = opt->n_drives +2;
     */

  if(opt->optbits & GET_A_FILENAME_AS_ARG){
    opt->filename = (char*)malloc(sizeof(char)*FILENAME_MAX);
    CHECK_ERR_NONNULL(opt->filename, "filename malloc");
    if(strcpy(opt->filename, argv[0]) == NULL){
      E("strcpy filename");
      return -1;
    }
    //opt->filename = argv[0];
  }
  //opt->points = (struct rec_point *)calloc(opt->n_drives, sizeof(struct rec_point));
#if(!DAEMON)
  if(opt->optbits & READMODE && !( opt->optbits & CAPTURE_W_DISK2FILE )){
    opt->hostname = (char*)malloc(sizeof(char)*IP_LENGTH);
    if(strcpy(opt->hostname, argv[1]) == NULL){
      E("strcpy hostname");
      return -1;
    }
    //opt->hostname = argv[1];
  }
  else
    opt->time = atoi(argv[1]);
#endif
  //*opt->cumul = 0;

  struct rlimit rl;
  /* Query max size */
  /* TODO: Doesn't work properly althought mem seems to be unlimited */
  ret = getrlimit(RLIMIT_DATA, &rl);
  if(ret < 0){
    LOGERR("Failed to get rlimit of memory\n");
    exit(1);
  }
#if(DEBUG_OUTPUT)
  LOG("STREAMER: Queried max mem size %ld \n", rl.rlim_cur);
#endif
  /* Check for memory limit						*/
  //unsigned long minmem = MIN_MEM_GIG*GIG;
  if (opt->minmem > rl.rlim_cur && rl.rlim_cur != RLIM_INFINITY){
#if(DEBUG_OUTPUT)
    LOG("STREAMER: Limiting memory to %lu\n", rl.rlim_cur);
#endif
    opt->minmem = rl.rlim_cur;
  }
  //if(!(opt->optbits & READMODE)){
  /*
     if (CALC_BUF_SIZE(opt) != 0)
     return -1;
     */
  //}
  return 0;
}
int init_rbufs(struct opt_s *opt){
  int i, err;
  err = CALC_BUF_SIZE(opt);
  CHECK_ERR("calc bufsize");
  D("nthreads as %d, which means %lu MB of used memory, packetsize: %lu each file has %d packets",, opt->n_threads, (opt->n_threads*opt->packet_size*opt->buf_num_elems)/(1024*1024), opt->packet_size, opt->buf_num_elems);
#ifdef TUNE_AFFINITY
  long processors = sysconf(_SC_NPROCESSORS_ONLN);
  D("Polled %ld processors",,processors);
  int cpusetter =2;
  CPU_ZERO(&opt->cpuset);
#endif

  /*
     if(opt->optbits & READMODE){
     }
     */
  opt->rbuf_pthreads = (pthread_t*)malloc(sizeof(pthread_t)*opt->n_threads);
  CHECK_ERR_NONNULL(opt->rbuf_pthreads, "pthreads malloc");

  opt->bes = (struct buffer_entity*)malloc(sizeof(struct buffer_entity)*opt->n_threads);
  CHECK_ERR_NONNULL(opt->bes, "buffer entity malloc");

#if(PPRIORITY)
  err = prep_priority(opt, RBUF_PRIO);
#endif

  D("Initializing buffer threads");

  for(i=0;i<opt->n_threads;i++){

    err = sbuf_init_buf_entity(opt, &(opt->bes[i]));
    CHECK_ERR("sbuf init");

    D("Starting buffer thread");
#if(PPRIORITY)
    err = pthread_create(&(opt->rbuf_pthreads[i]), &(opt->pta), (opt->bes[i]).write_loop,(void*)&(opt->bes[i]));
#else
    err = pthread_create(&(opt->rbuf_pthreads[i]), NULL, opt->bes[i].write_loop,(void*)&(opt->bes[i]));
#endif
    CHECK_ERR("pthread create");
#ifdef TUNE_AFFINITY
    if(cpusetter == processors)
      cpusetter = 1;
    CPU_SET(cpusetter,&(opt->cpuset));
    cpusetter++;

    D("Tuning buffer thread %i to processor %i",,i,cpusetter);
    err = pthread_setaffinity_np(opt->rbuf_pthreads[i], sizeof(cpu_set_t), &(opt->cpuset));
    if(err != 0){
      perror("Affinity");
      E("Error: setting affinity");
    }
    CPU_ZERO(&(opt->cpuset));
#endif //TUNE_AFFINITY
    D("Pthread number %d got id %lu",, i,opt->rbuf_pthreads[i]);
  }
  return 0;
}
int close_rbufs(struct opt_s *opt, struct stats* da_stats){
  int i,err, retval=0;
  // Stop the memory threads 
  oper_to_all(opt->membranch, BRANCHOP_STOPANDSIGNAL, NULL);
#ifndef UGLY_FIX_ON_RBUFTHREAD_EXIT
  for(i =0 ;i<opt->n_threads;i++){
    err = pthread_join(opt->rbuf_pthreads[i], NULL);
    if (err<0) {
      printf("ERROR; return code from pthread_join() is %d\n", err);
      retval--;
    }
    else
      D("%dth buffer exit OK",,i);
  }
#endif //UGLY_FIX_ON_RBUFTHREAD_EXIT
  free(opt->rbuf_pthreads);
  D("Getting stats and closing for membranch");
  oper_to_all(opt->membranch, BRANCHOP_CLOSERBUF, (void*)da_stats);

  free(opt->membranch);
  free(opt->bes);

  return retval;
}
int close_opts(struct opt_s *opt){
  //int i;
  if(opt->first_packet != NULL)
    free(opt->first_packet);
  if(opt->resqut != NULL)
    free(opt->resqut);
  if(opt->device_name != NULL)
    free(opt->device_name);
  if(opt->cfgfile != NULL){
    free(opt->cfgfile);
  }
  /*
  if(opt->optbits & READMODE){
    for(i = 0;i< opt->n_drives;i++){
      if(opt->filenames[i] != NULL)
	free(opt->filenames[i]);
    }
  }
  */
  if(opt->disk2fileoutput != NULL)
    free(opt->disk2fileoutput);
  if(opt->streamer_ent != NULL)
    free(opt->streamer_ent);
  if(opt->hostname != NULL)
    free(opt->hostname);
  if(opt->filename != NULL)
    free(opt->filename);
  config_destroy(&(opt->cfg));
#if(PPRIORITY)
  pthread_attr_destroy(&(opt->pta));
#endif
  if(opt->cumul != NULL)
    free(opt->cumul);
  if(opt->total_packets != NULL)
    free(opt->total_packets);
  free(opt);
  return 0;
}
#if(PPRIORITY)
#define CASEOF(x) case x:\
  D(#x);\
break;
int prep_priority(struct opt_s * opt, int priority){

  int err; 
  int realprio;
  int minprio,maxprio;
  int scheduler = sched_getscheduler(getpid());
  struct sched_param schedp;
#if(DEBUG_OUTPUT)
  switch(scheduler){	
    CASEOF(SCHED_OTHER)
    CASEOF(SCHED_BATCH)
    //CASEOF(SCHED_IDLE)
    CASEOF(SCHED_FIFO)
    CASEOF(SCHED_RR)
  }
#endif
  err = sched_getparam(getpid(), &schedp);

  minprio = sched_get_priority_min(scheduler);
  maxprio = sched_get_priority_max(scheduler);
  D("Min prio: %d, max prio: %d",, minprio, maxprio);
  //int halfprio = (maxprio+minprio)/2;

  /*
  int realprio = priority;

  if(priority == MAX_PRIO_FOR_PTHREAD)
    realprio = maxprio;
  else if (priority == MIN_PRIO_FOR_PTHREAD)
    realprio = minprio;
  else if (priority == RBUF_PRIO)
    realprio = halfprio;
  else
    realprio = halfprio +10;


  */
  realprio = schedp.sched_priority - priority;
  if(realprio < 0)
    realprio=0;
  D("Setting prio to %d",, realprio);

  memset(&(opt->param), 0, sizeof(struct sched_param));

  err = pthread_attr_getschedparam(&(opt->pta), &(opt->param));
  if(err != 0)
    E("Error getting schedparam for pthread attr: %s",,strerror(err));
  else
    D("Schedparam set to %d, Trying to set to %d",, opt->param.sched_priority, realprio);

  err = pthread_attr_setschedpolicy(&(opt->pta), scheduler);
  if(err != 0)
    E("Error setting schedtype for pthread attr: %s",,strerror(err));

  opt->param.sched_priority = realprio;
  err = pthread_attr_setschedparam(&(opt->pta), &(opt->param));
  if(err != 0)
    E("Error setting schedparam for pthread attr: %s",,strerror(err));
  err = pthread_attr_setinheritsched(&(opt->pta), PTHREAD_INHERIT_SCHED);
  if(err != 0)
    E("Error Setting inheritance");
  D("Done prepping priority");

  return 0;
}
#endif
/*
int prep_filenames(struct opt_s *opt){
  int i;
  D("preparing filenames");
  if(opt->optbits & READMODE){
    for(i=0;i<opt->n_drives;i++){
      opt->filenames[i] = malloc(sizeof(char)*FILENAME_MAX);
      CHECK_ERR_NONNULL(opt->filenames[i]
      //opt->filenames[i] = (char*)malloc(FILENAME_MAX);
      sprintf(opt->filenames[i], "%s%d%s%s%s", ROOTDIRS, i, "/", opt->filename,"/");
    }
  }
  D("filenames prepared");
  return 0;
}
*/
int prep_streamer(struct opt_s* opt){
  int err = 0;
  D("Initializing streamer thread");
  /* Format the capturing thread */
  opt->streamer_ent = (struct streamer_entity*)malloc(sizeof(struct streamer_entity));
  switch(opt->optbits & LOCKER_CAPTURE)
  {
    case CAPTURE_W_UDPSTREAM:
      if(opt->optbits & READMODE)
	err = udps_init_udp_sender(opt, opt->streamer_ent);
      else
	err = udps_init_udp_receiver(opt, opt->streamer_ent);
#if(DAEMON)
      opt->get_stats = udpstreamer_stats;
#endif
      break;
    case CAPTURE_W_FANOUT:
      err = fanout_init_fanout(opt, opt->streamer_ent);
      break;
    case CAPTURE_W_SPLICER:
      //err = sendfile_init_writer(&opt, &(streamer_ent));
      break;
    case CAPTURE_W_DUMMY:
      if(opt->optbits & READMODE)
	err = dummy_init_dummy_sender(opt, opt->streamer_ent);
      else
	err = dummy_init_dummy_receiver(opt, opt->streamer_ent);
#if(DAEMON)
      opt->get_stats = get_dummy_stats;
#endif
      break;
    case CAPTURE_W_DISK2FILE:
      err = d2f_init(opt, opt->streamer_ent);
      break;
    default:
      LOG("ERROR: Missing capture bit or two set! %lX\n", opt->optbits);
      break;

  }
  if(err != 0){
    LOGERR("Error in thread init\n");
    free(opt->streamer_ent);
    opt->streamer_ent = NULL;
    return -1;
  }
  return 0;
}
int prep_hostname(struct opt_s* opt){
  if(opt->hostname == NULL){
    E("Hostname is null!");
    return -1;
  }
  struct hostent *hostptr;

  hostptr = gethostbyname(opt->hostname);
  if(hostptr == NULL){
    perror("Hostname");
    //STREAMER_ERROR_EXIT;
    return -1;
  }
  memcpy(&(opt->serverip), (char *)hostptr->h_addr, sizeof(opt->serverip));

  D("Resolved hostname");
  return 0;
}
int init_recp(struct opt_s *opt){
  int err, i;
  opt->recs = (struct recording_entity*)malloc(sizeof(struct recording_entity)*opt->n_drives);
  CHECK_ERR_NONNULL(opt->recs, "rec entity malloc");
  for(i=0;i<opt->n_drives;i++){
    /*
     * NOTE: AIOW-stuff and udp-streamer are bidirectional and
     * only require the setting of opt->read to one for 
     * sending stuff
     */
    switch(opt->optbits & LOCKER_REC){
#if HAVE_LIBAIO
      case REC_AIO:
	err = aiow_init_rec_entity(opt, &(opt->recs[i]));
	//NOTE: elem_size is read inside if we're reading
	break;
#endif
      case REC_DUMMY:
	err = dummy_init_dummy(opt, &(opt->recs[i]));
	break;
      case REC_DEF:
	err = def_init_def(opt, &(opt->recs[i]));
	break;
      case REC_SPLICER:
	err = splice_init_splice(opt, &(opt->recs[i]));
	break;
      case REC_WRITEV:
	err = writev_init_rec_entity(opt, &(opt->recs[i]));
	break;
      default:
	E("Unknown recorder");
	err = -1;
	break;
    }
    if(err != 0){
      LOGERR("Error in writer init\n");
      /* TODO: Need to free all kinds of stuff if init goes bad */
      /* in the writer itself 					*/
      //free(re);
      //exit(-1);
    }
    /* Add the recording entity to the diskbranch */
  }
  switch(opt->optbits & LOCKER_REC){
#if HAVE_LIBAIO
    case REC_AIO:
      LOG("Created aio recpoints\n");
      break;
#endif
    case REC_DUMMY:
      LOG("Created dummy recpoints\n");
      break;
    case REC_DEF:
      LOG("Created default recpoints\n");
      break;
    case REC_SPLICER:
      LOG("Created splice recpoints\n");
      break;
  }
  return 0;
}
int close_recp(struct opt_s *opt, struct stats* da_stats){
  oper_to_all(opt->diskbranch, BRANCHOP_CLOSEWRITER, (void*)da_stats);
  free(opt->diskbranch);
  free(opt->recs);
  return 0;
}
#if(DAEMON)
void* vlbistreamer(void *opti)
#else
int main(int argc, char **argv)
#endif
{
  int err = 0;
  pthread_t streamer_pthread;
#ifdef HAVE_LRT
  struct timespec start_t;
#endif


#if(DAEMON)
  struct opt_s *opt = (struct opt_s*)opti;
  D("Starting thread from daemon");
#else
  LOG("Running in non-daemon mode\n");
  struct opt_s *opt = malloc(sizeof(struct opt_s));
  CHECK_ERR_NONNULL(opt, "opt malloc");
  LOG("STREAMER: Reading parameters\n");
  clear_and_default(opt,1);
  err = parse_options(argc,argv,opt);
  if(err != 0)
    STREAMER_ERROR_EXIT;

  /*
  err = prep_filenames(opt);
  if(err != 0)
    STREAMER_ERROR_EXIT;
    */

  err = init_branches(opt);
  CHECK_ERR("init branches");
  err = init_recp(opt);
  CHECK_ERR("init recpoints");

#ifdef HAVE_LIBCONFIG_H
  err = init_cfg(opt);
  //TODO: cfg destruction
  if(err != 0){
    E("Error in cfg init");
    STREAMER_ERROR_EXIT;
  }
#endif //HAVE_LIBCONFIG_H

  err = init_rbufs(opt);
  CHECK_ERR("init rbufs");
#endif //DAEMON
  /* Check and set cfgs at this point */
  //return -1;

  /* If we're sending stuff, check all the diskbranch members for the files they have 	*/
  /* Also updates the fileholders list to point the owners of files to correct drives	*/
#ifdef HAVE_LIBCONFIG_H
  if(opt->optbits &READMODE){
    oper_to_all(opt->diskbranch,BRANCHOP_CHECK_FILES,(void*)opt);
    LOG("For recording %s: %lu files were found out of %lu total. file index shows %ld files\n", opt->filename, opt->cumul_found, *opt->cumul, get_n_files(opt->fi));
    /* Only if we're sending live, do we need to rearrange these properly */
    /*
    if(opt->optbits & LIVE_SENDING){
      pthread_spin_lock(opt->augmentlock);
      arrange_by_id(opt);
      pthread_spin_unlock(opt->augmentlock);
    }
    */
  }
#endif //HAVE_LIBCONFIG_H


  /* Handle hostname etc */
  /* TODO: Whats the best way that accepts any format? */
  if(opt->hostname != NULL){
    err = prep_hostname(opt);
    if(err != 0){
      E("Error determining hostname");
      STREAMER_ERROR_EXIT;
    }
    D("Got hostname");
  }
  else if(opt->optbits & READMODE)
    D("No hostname set for readmode!");
  
  err = prep_streamer(opt);
  if(err != 0){
    STREAMER_ERROR_EXIT;
  }

  LOG("STREAMER: In main, starting receiver thread \n");

#if(PPRIORITY)
  
  if(opt->optbits & READMODE)
    prep_priority(opt, SEND_THREAD_PRIO);
  else
    prep_priority(opt, RECEIVE_THREAD_PRIO);
  /* TODO: err Not used */
  /*
  if(opt->optbits & READMODE)
    opt->param.sched_priority = MAX_PRIO_FOR_PTHREAD;
  else
    opt->param.sched_priority = RECEIVE_THREAD_PRIO;
  err = pthread_attr_setschedparam(&(opt->pta), &(opt->param));
  if(err != 0)
    E("Error setting schedparam for pthread attr: %s, to %d",, strerror(err), MAX_PRIO_FOR_PTHREAD);
  err = pthread_attr_setschedpolicy(&(opt->pta), SCHED_FIFO);
  if(err != 0)
    E("Error Setting sched policy to SCHED_FIFO");
  err = pthread_attr_setinheritsched(&(opt->pta), PTHREAD_INHERIT_SCHED);
  if(err != 0)
    E("Error Setting inheritance");
    */
  err = pthread_create(&streamer_pthread, &(opt->pta), opt->streamer_ent->start, (void*)opt->streamer_ent);
#else
  err = pthread_create(&streamer_pthread, NULL, opt->streamer_ent->start, (void*)opt->streamer_ent);
#endif

  if (err != 0){
    printf("ERROR; return code from pthread_create() is %d\n", err);
    STREAMER_ERROR_EXIT;
  }
  ///#if(DAEMON)
  opt->status = STATUS_RUNNING;
  //#endif

#ifdef TUNE_AFFINITY
  /* Put the capture on the first core */
  CPU_SET(0,&(opt->cpuset));
  /*
     cpusetter++;
     if(cpusetter > processors)
     cpusetter = 1;
     */

  err = pthread_setaffinity_np(streamer_pthread, sizeof(cpu_set_t), &cpuset);
  if(err != 0){
    E("Error: setting affinity: %d",,err);
  }
  CPU_ZERO(&cpuset);
#endif

  if(opt->optbits & READMODE){
#ifdef HAVE_LRT
    clock_gettime(CLOCK_REALTIME, &start_t);
#else
    //TODO
#endif
  }
#if(!DAEMON)
  /* Print speed etc. */
  if(opt->optbits & VERBOSE){

    /* Init the stats */
    struct stats *stats_prev, *stats_now;//, stats_temp;
    stats_prev = (struct stats*)malloc(sizeof(struct stats));
    STREAMER_CHECK_NONNULL(stats_prev, "stats malloc");
    stats_now = (struct stats*)malloc(sizeof(struct stats));
    //STREAMER_CHECK_NONNULL(stats_now, "stats malloc");
    //memset(stats_prev, 0,sizeof(struct stats));
    //memset(stats_now, 0,sizeof(struct stats));
    /* MEmset is doing weird stuff 	*/
    init_stats(stats_prev);
    init_stats(stats_now);
    int sleeptodo;
    //memset(&stats_now, 0,sizeof(struct stats));
    LOG("STREAMER: Printing stats per second\n");
    LOG("----------------------------------------\n");

    if(opt->optbits & READMODE)
      sleeptodo= 1;
    else
      sleeptodo = opt->time;
    while(sleeptodo >0 && (opt->status & STATUS_RUNNING)){
      sleep(1);
      //memset(stats_now, 0,sizeof(struct stats));
      init_stats(stats_now);
      //print_midstats(tempsched, stats_prev);

      opt->streamer_ent->get_stats(opt->streamer_ent->opt, stats_now);
      /* Query and print the stats */
      /*
	 for(i=0;i<opt.n_threads;i++){
      //threads[i].get_stats(threads[i].opt, &stats_now);
      if(threads[i].be->recer->get_stats != NULL)
      threads[i].be->recer->get_stats(threads[i].be->recer->opt, &stats_now);
      }
      */
      //TODO: Write end stats
      oper_to_all(opt->diskbranch,BRANCHOP_GETSTATS,(void*)stats_now);

      //memcpy(&stats_temp, &stats_now, sizeof(struct stats));
      neg_stats(stats_now, stats_prev);

      print_intermediate_stats(stats_now);
      //LOG("Time %ds \t------------------------\n", opt.time-sleeptodo+1);
      if(!(opt->optbits & READMODE)){
	LOG("Time %lds\n", opt->time-sleeptodo+1);
	//LOG("Files: %ld\n", stats_now.files_exchanged);
      }
      else{
	LOG("Time %ds\n", sleeptodo);
	//LOG("Files: %ld/%ld\n", stats_now.files_exchanged, opt->cumul_found);
      }

      LOG("Ringbuffers: ");
      print_br_stats(opt->membranch);
      LOG("Recpoints: ");
      print_br_stats(opt->diskbranch);

      LOG("----------------------------------------\n");

      if(!(opt->optbits & READMODE))
	sleeptodo--;
      else
	sleeptodo++;
      add_stats(stats_prev, stats_now);
      //memcpy(&stats_prev, &stats_temp, sizeof(struct stats));
      /*
	 if(opt.optbits & READMODE){
	 if(opt.cumul >= opt.max_num_packets-1)
	 sleeptodo = 0;
	 }
	 */
      fflush(stdout);
    }
    free(stats_now);
    //free(tempsched);
    free(stats_prev);
  }
  if(!(opt->optbits & READMODE)){
    if(!(opt->optbits & VERBOSE))
      sleep(opt->time);
    shutdown_thread(opt);
  }
  /* If we're capturing, time the threads and run them down after we're done 	*/
  else
#endif /* DAEMON */
  {
    /* Check also that last_packet is 0. Else the thread should shut itself 	*/
    /* down									*/
    if(!(opt->optbits & READMODE) && opt->last_packet == 0){
      int sleepleft = opt->time;
      //while(sleepleft > 0 && opt->streamer_ent->is_running(opt->streamer_ent)){
      while(sleepleft > 0 && (opt->status & STATUS_RUNNING)){
	D("Slept really");
	sleep(1);
	sleepleft-=1;
      }
      //sleep(opt->time);
      ////pthread_mutex_destroy(opt.cumlock);
      shutdown_thread(opt);
    }
  }

  err = pthread_join(streamer_pthread, NULL);
  if (err<0) {
    printf("ERROR; return code from pthread_join() is %d\n", err);
  }
  else
    D("Streamer thread exit OK");
  LOG("STREAMER: Threads finished. Getting stats\n");

  if(opt->optbits & READMODE){
    /* Too fast sending so I'll keep this in ticks and use floats in stats */
#ifdef HAVE_LRT
    struct timespec end_t;
    clock_gettime(CLOCK_REALTIME, &end_t);
    opt->time = ((end_t.tv_sec * BILLION + end_t.tv_nsec) - (start_t.tv_sec*BILLION + start_t.tv_nsec))/BILLION;
    //LOG("END: %lus %luns, START: %lus, %luns\n", end_t.tv_sec, end_t.tv_nsec, start_t.tv_sec, start_t.tv_nsec);
#else
    LOGERR("STREAMER: lrt not present. Setting time to 1\n");
    opt->time = 1;
    //opt.time = (clock() - start_t);
#endif
  }

  LOG("Blocking until owned buffers are released\n");
  block_until_free(opt->membranch, opt);
#if(DAEMON)
  LOG("Buffers finished\n");
  if(opt->status != STATUS_STOPPED)
  {
    E("Thread didnt finish nicely with STATUS_STOPPED");
    opt->status |= STATUS_FINISHED;
  }
  else
    opt->status = STATUS_FINISHED;
#else
  close_streamer(opt);
#endif

#if(DAEMON)
  D("Streamer thread exiting");
  pthread_exit(NULL);
#else
  close_opts(opt);
  STREAMER_EXIT;
#endif
}
int close_streamer(struct opt_s *opt){
  struct stats* stats_full = (struct stats*)malloc(sizeof(struct stats));
  init_stats(stats_full);
  if(opt->streamer_ent != NULL)
    opt->streamer_ent->close(opt->streamer_ent->opt, (void*)stats_full);
#if(!DAEMON)
  close_rbufs(opt, stats_full);
  close_recp(opt,stats_full);
  D("Membranch and diskbranch shut down");
#else
  //oper_to_all(opt->diskbranch,BRANCHOP_GETSTATS,(void*)stats_full);
  stats_full->total_written = opt->bytes_exchanged;
  //free(opt->augmentlock);
#endif
  D("Printing stats");
  print_stats(stats_full, opt);
  free(stats_full);
  D("Stats over");

  return 0;
}
/* These two separated here */
int write_cfg(config_t *cfg, char* filename){
  int err = config_write_file(cfg,filename);
  if(err == CONFIG_FALSE){
    E("Failed to write CFG to %s",,filename);
    return -1;
  }
  else
    return 0;
}
int write_cfg_for_rec(struct opt_s * opt, char* filename){
  int err;
  config_t cfg;
  config_init(&cfg);
  stub_rec_cfg(config_root_setting(&cfg),opt);
  err = write_cfg(&cfg, filename);
  config_destroy(&cfg);
  return err;
}
int read_cfg(config_t *cfg, char * filename){
  int err = config_read_file(cfg,filename);
  if(err == CONFIG_FALSE){
    E("%s:%d - %s",, filename, config_error_line(cfg), config_error_text(cfg));
    E("Failed to read CFG from %s",,filename);
    return -1;
  }
  else
    return 0;
}
int init_branches(struct opt_s *opt){
  int err;
  opt->membranch = (struct entity_list_branch*)malloc(sizeof(struct entity_list_branch));
  CHECK_ERR_NONNULL(opt->membranch, "membranch malloc");
  opt->diskbranch = (struct entity_list_branch*)malloc(sizeof(struct entity_list_branch));
  CHECK_ERR_NONNULL(opt->diskbranch, "diskbranch malloc");

  opt->diskbranch->mutex_free = 0;
  opt->membranch->mutex_free = 0;

  opt->membranch->freelist = NULL;
  opt->membranch->busylist = NULL;
  opt->membranch->loadedlist = NULL;
  opt->diskbranch->freelist = NULL;
  opt->diskbranch->busylist = NULL;
  opt->diskbranch->loadedlist = NULL;

  err = LOCK_INIT(&(opt->membranch->branchlock));
  CHECK_ERR("branchlock");
  err = LOCK_INIT(&(opt->diskbranch->branchlock));
  CHECK_ERR("branchlock");
  err = pthread_cond_init(&(opt->membranch->busysignal), NULL);
  CHECK_ERR("busysignal");
  err = pthread_cond_init(&(opt->diskbranch->busysignal), NULL);
  CHECK_ERR("busysignal");
  return 0;
}
void shutdown_thread(struct opt_s *opt){
  if(opt->streamer_ent != NULL){
    opt->streamer_ent->stop(opt->streamer_ent);
  }
}
#if(DAEMON)
int print_midstats(struct schedule* sched, struct stats* old_stats)
{
  TIMERTYPE temptime;
  struct listed_entity *le= sched->br.busylist;
  struct stats tempstats;
  GETTIME(temptime);
  LOG("Time:\t%lu\n", temptime.tv_sec);
  while(le != NULL){
    struct scheduled_event * ev = (struct scheduled_event*)le->entity;
    if(ev->opt->get_stats != NULL){
      init_stats(&tempstats);
      ev->opt->get_stats((void*)ev->opt, (void*)&tempstats);
      neg_stats(&tempstats, ev->stats);
      LOG("Event:\t%s\t", ev->opt->filename);
      LOG("Network:\t%luMb/s\tDropped %lu\tIncomplete %lu\n"
	  ,BYTES_TO_MBITSPS(tempstats.total_bytes),tempstats.dropped, tempstats.incomplete);
      add_stats(ev->stats, &tempstats);
    }
    le = le->child;
  }
  init_stats(&tempstats);
  oper_to_all(sched->default_opt->diskbranch,BRANCHOP_GETSTATS,(void*)&tempstats);
  neg_stats(&tempstats, old_stats);
  LOG("HD-Speed:\t%luMB/s\n",BYTES_TO_MBITSPS(tempstats.total_written));
  add_stats(old_stats, &tempstats);

  LOG("Ringbuffers: ");
  print_br_stats(sched->default_opt->membranch);
  LOG("Recpoints: ");
  print_br_stats(sched->default_opt->diskbranch);

  LOG("----------------------------------------\n");
  return 0;
}
#endif
/* Generic function which we use after we get the opt	*/
inline int iden_from_opt(struct opt_s *opt, void* val1, void* val2, int iden_type){
  (void)val2;
  switch (iden_type){
    case CHECK_BY_NAME:
      //char* name = (char*)val1;
      if(strcmp(opt->filename,(char*)val1)== 0)
	return 1;
      else 
	return 0;
      break;
    case CHECK_BY_OPTPOINTER:
      if(opt == (struct opt_s*) val1)
	return 1;
      else 
	return 0;
      break;
    default:
      E("Identification type not recognized!");
      return 0;
      break;
  }
}
