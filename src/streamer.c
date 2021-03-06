
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
#include <sys/syscall.h>
#include <sys/file.h>
#include <sys/resource.h>       /*Query max allocatable memory */
#include <libconfig.h>
//TODO: Add explanations for includes
#include <netdb.h>              // struct hostent
#include <time.h>
#include <sched.h>
#include "config.h"
#include "logging_main.h"
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
#include "sendfile_writer.h"
#include "writev_writer.h"
#include "disk2file.h"
#include "resourcetree.h"
#include "confighelper.h"
#include "simplebuffer.h"
#include "dummywriter.h"
#include "dummy_stream.h"
#include "tcp_stream.h"
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
#define STREAMER_EXIT set_status_for_opt(opt,STATUS_FINISHED); pthread_exit(NULL)
#define STREAMER_ERROR_EXIT set_status_for_opt(opt,STATUS_ERROR); pthread_exit(NULL)
#else
#define STREAMER_EXIT return 0
#define STREAMER_ERROR_EXIT return -1
#endif

#define FREE_AND_ERROREXIT if(opt->device_name != NULL){free(opt->device_name);} config_destroy(&(opt->cfg)); free(opt->membranch); free(opt->diskbranch); exit(-1);

/* This should be more configurable */
extern char *optarg;
extern int optind, optopt;

FILE *logfile;

void udpstreamer_stats(void *opts, void *statsi)
{
  struct opt_s *opt = (struct opt_s *)opts;
  struct stats *stats = (struct stats *)statsi;
  opt->streamer_ent->get_stats(opt->streamer_ent->opt, stats);
  //stats->total_written += opt->bytes_exchanged;
}

int calculate_buffer_sizes(struct opt_s *opt)
{
  /* Calc how many elementes we get into the buffer to fill the minimun */
  /* amount of memory we want to use                                    */

  /* Magic is the n of blocks we wan't to divide the ringbuffer to      */
  opt->buf_division = 8;
  //unsigned long bufsize;// = opt.packet_size;
  int found = 0;

  int extra = 0;
  if (opt->optbits & USE_RX_RING)
    {
      while ((opt->packet_size % 16) != 0)
        {
          if (opt->optbits & READMODE)
            E("Shouldn't need this in sending with RX-ring!");
          opt->packet_size++;
          extra++;
        }
      D("While using RX-ring we need to reserve %d extra bytes per buffer element", extra);
    }

  /* TODO: do_w_stuff gets warped  from MB to num of elems */
  LOG("STREAMER: Calculating total buffer size between "
      "%lu GB to %luGB,"
      " size %lu packets, "
      "Doing maximum %luMB size writes\n", opt->minmem, opt->maxmem,
      opt->packet_size, opt->do_w_stuff_every / MEG);
  /* Set do_w_stuff to minimum wanted */
  /* First set do_w_stuff to be packet aligned */
  unsigned long temp = opt->do_w_stuff_every / opt->packet_size;
  LOG("%lu\n", temp);
  opt->do_w_stuff_every = temp * (opt->packet_size);

  /* Increase block division to fill min amount of memory */
  while ((opt->do_w_stuff_every) * opt->buf_division * (opt->n_threads) <
         (opt->minmem) * GIG)
    {
      opt->buf_division++;
    }
  /* Store for later use if proper size not found with current opt->buf_division */
  temp = opt->do_w_stuff_every;
  while ((found == 0) && (opt->buf_division > 0))
    {
      /* Increase buffer size until its BLOCK_ALIGNed */
      while ((opt->do_w_stuff_every) * opt->buf_division * (opt->n_threads) <
             (opt->maxmem) * GIG)
        {
          if (opt->do_w_stuff_every % BLOCK_ALIGN == 0)
            {
              found = 1;
              opt->buf_num_elems =
                (opt->do_w_stuff_every * opt->buf_division) /
                opt->packet_size;
              //opt->do_w_stuff_every = opt->do_w_stuff_every/opt->packet_size;
              break;
            }
          opt->do_w_stuff_every += opt->packet_size;
        }
      if (found == 0)
        {
          opt->do_w_stuff_every = temp;
          opt->buf_division--;
        }
    }
  if (found == 0)
    {
      LOGERR("STREAMER: Didnt find Alignment"
             "%lu GB to %luGB"
             ", Each buffer having %lu bytes"
             ", Writing in %lu size blocks"
             ", %d Blocks per buffer"
             ", Elements in buffer %d\n", opt->minmem, opt->maxmem,
             opt->packet_size * (opt->buf_num_elems), opt->do_w_stuff_every,
             opt->buf_division, opt->buf_num_elems);
      //LOG("STREAMER: Didnt find alignment for %lu on %d threads, with w_every %lu\n", opt->packet_size,opt->n_threads, (opt->packet_size*(opt->buf_num_elems))/opt->buf_division);
      return -1;
    }
  else
    {
      if (opt->optbits & USE_RX_RING)
        {
          D("The 16 aligned restriction of RX-ring resulted in %ld MB larger memory use", extra * opt->buf_num_elems * opt->n_threads / MEG);
        }

      LOG("STREAMER: Alignment found between "
          "%lu GB to %luGB"
          ", Each buffer having %lu MB"
          ", Writing in %lu MB size blocks"
          ", Elements in buffer %d"
          ", Total used memory: %luMB\n", opt->minmem, opt->maxmem,
          (opt->packet_size * (opt->buf_num_elems)) / MEG,
          (opt->do_w_stuff_every) / MEG, opt->buf_num_elems,
          (opt->buf_num_elems * opt->packet_size * opt->n_threads) / MEG);
      //LOG("STREAMER: Alignment found for %lu size packet with %d threads at %lu with ringbuf in %lu blocks. hd write size as %lu\n", opt->packet_size,opt->n_threads ,opt->buf_num_elems*(opt->packet_size),opt->buf_division, (opt->buf_num_elems*opt->packet_size)/opt->buf_division);
      return 0;
    }
}

int rxring_packetadjustment(struct opt_s *opt)
{
  int extra = 0;
  while ((opt->packet_size % 16) != 0)
    {
      if (opt->optbits & READMODE)
        E("Shouldn't need this in sending with RX-ring!");
      opt->packet_size++;
      extra++;
    }
  D("While using RX-ring we need to reserve %d extra bytes per buffer element", extra);
  return extra;
}

int calculate_buffer_sizes_simple(struct opt_s *opt)
{
  /* A very simple buffer size calculator that fixes the filesizes to   */
  /* constants according to the packet size. We try to keep the         */
  /*filesize between 256 and 512 and must keep it packet- and           */
  /* blockaligned at all times                                          */
  //int extra= 0;
  if (opt->optbits & USE_RX_RING)
    {
      rxring_packetadjustment(opt);
    }
  opt->buf_division = B(3);
  while (opt->packet_size * BLOCK_ALIGN * opt->buf_division <=
         opt->filesize * MEG)
    opt->buf_division <<= 1;
  opt->buf_num_elems = (BLOCK_ALIGN * opt->buf_division);
  opt->do_w_stuff_every = (BLOCK_ALIGN * opt->packet_size);

  opt->n_threads = 1;
  while (opt->n_threads * opt->packet_size * opt->buf_num_elems <
         opt->maxmem * GIG)
    opt->n_threads++;
  opt->n_threads -= 1;

  return 0;
}

int calculate_buffer_sizes_singlefilesize(struct opt_s *opt)
{
//int extra= 0;
  if (opt->optbits & USE_RX_RING)
    {
      rxring_packetadjustment(opt);
    }
  opt->buf_num_elems = (opt->filesize) / opt->packet_size;
  opt->n_threads = (opt->maxmem * GIG) / opt->filesize;
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
static void usage(char *binary)
{
  LOGERR("usage: %s [OPTIONS]... name (time to receive / host to send to)\n"
         "-A MAXMEM	Use maximum MAXMEM amount of memory for ringbuffers(default 12GB)\n"
#ifdef HAVE_RATELIMITER
         "-a MYY		Wait MYY microseconds between packet sends\n"
#endif
         "-t {fanout|udpstream|sendfile|TODO	Capture type(Default: udpstream)(sendfile is a prototype not yet in kernel)(fanout doesn't write to disk. Poor performance)\n"
         "-c CFGFILE	Load config from cfg-file CFGFILE\n"
         "-f LOGFILE    Log everything to file\n"
         //"-a {lb|hash}   Fanout type(Default: lb)\n"
         "-d DRIVES	Number of drives(Default: 1)\n"
         "-i INTERFACE	Which interface to bind to(Not required)\n"
         //"-I MINMEM      Use at least MINMEM amount of memory for ringbuffers(default 4GB)\n"
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
         //"-V             Verbose. Print stats on individual mountpoint transfers\n"
         //"-W WRITEEVERY  Try to do HD-writes every WRITEEVERY MB(default 16MB)\n"
         "-w {"
#ifdef HAVE_LIBAIO
         "aio|"
#endif
         "def|splice|dummy}	Choose writer to use(Default: def)\n"
         "-x 		Use an mmap rxring for receiving\n", binary);
}

/* Why don't I just memset? */
void init_stats(struct stats *stats)
{
  memset(stats, 0, sizeof(struct stats));
  /*
     stats->total_bytes = 0;
     stats->incomplete = 0;
     stats->total_written = 0;
     stats->total_packets = 0;
     stats->dropped = 0;
   */
}

void neg_stats(struct stats *st1, struct stats *st2)
{
  /* We sometimes get a situation, where the previous val is larger     */
  /* than the new value. This shouldn't happen! For now I'll just add   */
  /* an ugly hack here. TODO: Solve                                     */
  /* NOTE: This doesn't affect the final stats                          */
#ifdef UGLY_HACKS_ON_STATS
  if (st1->total_bytes < st2->total_bytes)
    st1->total_bytes = 0;
  else
#endif
    st1->total_bytes -= st2->total_bytes;
  st1->incomplete -= st2->incomplete;
#ifdef UGLY_HACKS_ON_STATS
  if (st1->total_written < st2->total_written)
    st1->total_written = 0;
  else
#endif
    st1->total_written -= st2->total_written;
#ifdef UGLY_HACKS_ON_STATS
  if (st1->total_packets < st2->total_packets)
    st1->total_packets = 0;
  else
#endif
    st1->total_packets -= st2->total_packets;
  st1->dropped -= st2->dropped;
}

void add_stats(struct stats *st1, struct stats *st2)
{
  st1->total_bytes += st2->total_bytes;
  st1->incomplete += st2->incomplete;
  st1->total_written += st2->total_written;
  st1->dropped += st2->dropped;
}

void print_intermediate_stats(struct stats *stats)
{
  LOG("Net Send/Receive completed: \t%luMb/s\n"
      "HD Read/write completed \t%luMb/s\n"
      "Dropped %lu\tIncomplete %lu\n", BYTES_TO_MBITSPS(stats->total_bytes),
      BYTES_TO_MBITSPS(stats->total_written), stats->dropped,
      stats->incomplete);
}

void print_stats(struct stats *stats, struct opt_s *opts)
{
  float precisetime = floatdiff(&(opts->endtime), &(opts->starting_time));
  if (opts->optbits & READMODE)
    {
      LOG("Stats for %s \n"
          "Packets: %lu\n"
          "Bytes sent: %lu\n"
          "Bytes Read: %lu\n"
          "Sendtime: %.2fs\n"
          "Files: %lu\n"
          "HD-failures: %d\n"
          "Net send Speed: %5.0fMb/s\n"
          "HD read Speed: %5.0fMb/s\n", opts->filename, stats->total_packets,
          stats->total_bytes, stats->total_written, precisetime, opts->cumul,
          opts->hd_failures,
          (((float)stats->total_bytes) * 8) / (1024 * 1024 * precisetime),
          (((float)stats->total_written) * 8) / (1024 * 1024 * precisetime));
    }
  else
    {
      if (precisetime == 0)
        E("SendTime is 0. Something went wrong");
      else
        LOG("Stats for %s \n"
            "Packets: %lu\n"
            "Bytes: %lu\n"
            "Dropped: %lu\n"
            "Incomplete: %lu\n"
            "Written: %lu\n"
            "Recvtime: %.2fs\n"
            "Files: %lu\n"
            "HD-failures: %d\n"
            "Net receive Speed: %5.0fMb/s\n"
            "HD write Speed: %5.0fMb/s\n", opts->filename,
            stats->total_packets, stats->total_bytes, stats->dropped,
            stats->incomplete, stats->total_written, precisetime, opts->cumul,
            opts->hd_failures,
            (((float)stats->total_bytes) * 8) / (1024 * 1024 * precisetime),
            (((float)stats->total_written) * 8) / (1024 * 1024 *
                                                   precisetime));
    }
}

/* Defensive stuff to check we're not copying stuff from default	*/
int clear_pointers(struct opt_s *opt)
{
  opt->filename = NULL;
  opt->address_to_bind_to = NULL;
  opt->hostname = NULL;
  opt->cfgfile = NULL;
  opt->disk2fileoutput = NULL;
  return 0;
}

int clear_and_default(struct opt_s *opt, int create_cfg)
{
  memset(opt, 0, sizeof(struct opt_s));
#if(DAEMON)
  opt->status = STATUS_NOT_STARTED;
#if(PROTECT_STATUS_W_RWLOCK)
  pthread_rwlock_init(&(opt->statuslock), NULL);
#endif
#endif

  if (create_cfg == 1)
    config_init(&(opt->cfg));

  /* Opts using optbits */
  opt->optbits |= CAPTURE_W_UDPSTREAM;
  opt->do_w_stuff_every = HD_MIN_WRITE_SIZE;
  opt->root_pid = getpid();
  opt->port = 2222;
  opt->n_drives = 1;
  opt->packet_size = DEF_BUF_ELEM_SIZE;
  opt->optbits |= DATATYPE_UNKNOWN;

  opt->optbits |= BUFFER_SIMPLE;
  opt->optbits |= REC_DEF;
  opt->rate = 10000;
  opt->minmem = MIN_MEM_GIG;
  opt->maxmem = MAX_MEM_GIG;
  opt->optbits |= SIMPLE_BUFFER;

  opt->stream_multiply = 1;

#if(!DAEMON)
  opt->optbits |= GET_A_FILENAME_AS_ARG;
#endif

  return 0;
}

int log_to_file(const char *path)
{
  LOG("Logging to %s\n", path);
  file_out = fopen(path, "a");
  if (!file_out)
    {
      file_out = stdout;
      E("Failed to open logfile %s: %s", path, strerror(errno));
      return -1;
    }
  file_err = file_out;
  fclose(stdout);
  fclose(stderr);
  return 0;
}

int parse_options(int argc, char **argv, struct opt_s *opt)
{
  int ret;
  while ((ret = getopt(argc, argv, "d:i:t:s:n:m:w:p:q:ur:a:vVI:A:W:xc:hf:"))
         != -1)
    {
      switch (ret)
        {
        case 'i':
          opt->address_to_bind_to = strdup(optarg);
          break;
        case 'c':
          opt->cfgfile = (char *)malloc(sizeof(char) * FILENAME_MAX);
          //CHECK_ERR_NONNULL(opt->cfgfile, "Cfgfile malloc");
          LOG
            ("Path for cfgfile specified. All command line options before this argument might be ignored\n");
          ret = read_full_cfg(opt);
          if (ret != 0)
            {
              E("Error parsing cfg file. Exiting");
              free(opt->cfgfile);
              return -1;
            }
          break;
        case 'v':
          opt->optbits |= VERBOSE;
          break;
        case 'f':
          LOG("Logging to %s\n", optarg);
          fflush(file_out);
          file_out = fopen(optarg, "a");
          if (!file_out)
            {
              E("Failed to open log file %s: %s", optarg, strerror(errno));
              return -1;
            }
          file_err = file_out;
          break;
        case 'h':
          usage(argv[0]);
          return -1;
          break;
        case 'd':
          opt->n_drives = atoi(optarg);
          break;
#if(DAEMON)
        case 'e':
          GETSECONDS(opt->starting_time) = atoi(optarg);
          break;
#endif
        case 'I':
          opt->minmem = atoi(optarg);
          break;
        case 'x':
          opt->optbits |= USE_RX_RING;
          break;
        case 'W':
          opt->do_w_stuff_every = atoi(optarg) * MEG;
          break;
        case 'A':
          opt->maxmem = atoi(optarg);
          break;
        case 'V':
          opt->optbits |= MOUNTPOINT_VERBOSE;
          break;
        case 't':
          opt->optbits &= ~LOCKER_CAPTURE;
          if (!strcmp(optarg, "fanout"))
            {
              //opt->capture_type = CAPTURE_W_FANOUT;
              opt->optbits |= CAPTURE_W_FANOUT;
            }
          else if (!strcmp(optarg, "udpstream"))
            {
              //opt->capture_type = CAPTURE_W_UDPSTREAM;
              opt->optbits |= CAPTURE_W_UDPSTREAM;
            }
          /*
             else if (!strcmp(optarg, "sendfile")){
             //opt->capture_type = CAPTURE_W_SPLICER;
             opt->optbits |= CAPTURE_W_SPLICER;
             }
           */
          else if (!strcmp(optarg, "dummy"))
            {
              //opt->capture_type = CAPTURE_W_SPLICER;
              opt->optbits |= CAPTURE_W_DUMMY;
            }
          else
            {
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
          opt->wait_nanoseconds = atoi(optarg) * 1000;
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
          opt->hostname = (char *)malloc(sizeof(char) * IP_LENGTH);
          if (strcpy(opt->hostname, optarg) == NULL)
            {
              E("strcpy hostname");
              return -1;
            }
          //opt->n_threads = atoi(optarg);
          break;
        case 'q':
          opt->optbits &= ~LOCKER_DATATYPE;
          if (!strcmp(optarg, "vdif"))
            {
              D("Datatype as VDIF");
              opt->optbits |= DATATYPE_VDIF;
            }
          else if (!strcmp(optarg, "mark5b"))
            {
              D("Datatype as Mark5");
              opt->optbits |= DATATYPE_MARK5B;
            }
          else if (!strcmp(optarg, "udpmon"))
            {
              D("Datatype as UDPMON");
              opt->optbits |= DATATYPE_UDPMON;
            }
          else if (!strcmp(optarg, "none"))
            {
              D("Datatype as none");
              opt->optbits |= DATATYPE_UNKNOWN;
            }
          else
            {
              E("Unknown datatype %s", optarg);
              opt->optbits |= DATATYPE_UNKNOWN;
            }
          break;
        case 'm':
          if (!strcmp(optarg, "r"))
            {
              opt->optbits &= ~READMODE;
              //opt->read = 0;
            }
          else if (!strcmp(optarg, "s"))
            {
              //opt->read = 1;
              opt->optbits |= READMODE;
            }
          else
            {
              LOGERR("Unknown mode type [%s]\n", optarg);
              usage(argv[0]);
              exit(1);
            }
          break;
        case 'w':
          opt->optbits &= ~LOCKER_REC;
          if (!strcmp(optarg, "def"))
            {
              /*
                 opt->rec_type = REC_DEF;
                 opt->async = 0;
               */
              opt->optbits |= REC_DEF;
              opt->optbits &= ~ASYNC_WRITE;
            }
#ifdef HAVE_LIBAIO
          else if (!strcmp(optarg, "aio"))
            {
              /*
                 opt->rec_type = REC_AIO;
                 opt->async = 1;
               */
              opt->optbits |= REC_AIO | ASYNC_WRITE;
            }
#endif
          else if (!strcmp(optarg, "splice"))
            {
              /*
                 opt->rec_type = REC_SPLICER;
                 opt->async = 0;
               */
              opt->optbits |= REC_SPLICER;
              opt->optbits &= ~ASYNC_WRITE;
            }
          else if (!strcmp(optarg, "dummy"))
            {
              /*
                 opt->rec_type = REC_DUMMY;
                 opt->buf_type = WRITER_DUMMY;
               */
              opt->optbits &= ~LOCKER_WRITER;
              opt->optbits |= REC_DUMMY | WRITER_DUMMY;
              opt->optbits &= ~ASYNC_WRITE;
            }
          else
            {
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
  if (argc - optind != 2)
    {
      usage(argv[0]);
      exit(1);
    }
#endif
  argv += optind;
  //argc -=optind;

  /* TODO: Enable giving a custom cfg-file on invocation */
#if(!DAEMON)
  if (opt->cfgfile != NULL)
    {
    }
#endif //DAEMON

  /* If we're using rx-ring, then set the packet size to +TPACKET_HDRLEN */
  /*
     if(opt->optbits & USE_RX_RING)
     opt->packet_size += TPACKET_HDRLEN;
   */

  /* If n_threads isn't set, set it to n_drives +2      */
  /* Not used. Maxmem limits this instead               */
  /*
     if(opt->n_threads == 0)
     opt->n_threads = opt->n_drives +2;
   */

  if (opt->optbits & GET_A_FILENAME_AS_ARG)
    {
      opt->filename = (char *)malloc(sizeof(char) * FILENAME_MAX);
      CHECK_ERR_NONNULL(opt->filename, "filename malloc");
      if (strcpy(opt->filename, argv[0]) == NULL)
        {
          E("strcpy filename");
          return -1;
        }
      //opt->filename = argv[0];
    }
  //opt->points = (struct rec_point *)calloc(opt->n_drives, sizeof(struct rec_point));
#if(!DAEMON)
  if (opt->optbits & READMODE && !(opt->optbits & CAPTURE_W_DISK2FILE))
    {
      opt->hostname = (char *)malloc(sizeof(char) * IP_LENGTH);
      if (strcpy(opt->hostname, argv[1]) == NULL)
        {
          E("strcpy hostname");
          return -1;
        }
      //opt->hostname = argv[1];
    }
  else
    opt->time = atoi(argv[1]);
#endif

  struct rlimit rl;
  /* Query max size */
  /* TODO: Doesn't work properly althought mem seems to be unlimited */
  ret = getrlimit(RLIMIT_DATA, &rl);
  if (ret < 0)
    {
      LOGERR("Failed to get rlimit of memory\n");
      exit(1);
    }
#if(DEBUG_OUTPUT)
  LOG("STREAMER: Queried max mem size %ld \n", rl.rlim_cur);
#endif
  /* Check for memory limit                                             */
  //unsigned long minmem = MIN_MEM_GIG*GIG;
  if (opt->minmem > rl.rlim_cur && rl.rlim_cur != RLIM_INFINITY)
    {
#if(DEBUG_OUTPUT)
      LOG("STREAMER: Limiting memory to %lu\n", rl.rlim_cur);
#endif
      opt->minmem = rl.rlim_cur;
    }
  return 0;
}

int init_rbufs(struct opt_s *opt)
{
  int i, err;
  err = CALC_BUF_SIZE(opt);
  CHECK_ERR("calc bufsize");
  D("nthreads as %d, which means %lu MB of used memory, packetsize: %lu each file has %d packets", opt->n_threads, (opt->n_threads * opt->packet_size * opt->buf_num_elems) / (1024 * 1024), opt->packet_size, opt->buf_num_elems);
#ifdef TUNE_AFFINITY
  long processors = sysconf(_SC_NPROCESSORS_ONLN);
  D("Polled %ld processors", processors);
  int cpusetter = 2;
  CPU_ZERO(&opt->cpuset);
#endif

  /*
     if(opt->optbits & READMODE){
     }
   */
  opt->rbuf_pthreads =
    (pthread_t *) malloc(sizeof(pthread_t) * opt->n_threads);
  CHECK_ERR_NONNULL(opt->rbuf_pthreads, "pthreads malloc");

  opt->bes =
    (struct buffer_entity *)malloc(sizeof(struct buffer_entity) *
                                   opt->n_threads);
  CHECK_ERR_NONNULL(opt->bes, "buffer entity malloc");

  D("Initializing buffer threads");

  for (i = 0; i < opt->n_threads; i++)
    {

      err = sbuf_init_buf_entity(opt, &(opt->bes[i]));
      CHECK_ERR("sbuf init");

      D("Starting buffer thread");
      err =
        pthread_create(&(opt->rbuf_pthreads[i]), NULL, opt->bes[i].write_loop,
                       (void *)&(opt->bes[i]));
      CHECK_ERR("pthread create");
#ifdef TUNE_AFFINITY
      if (cpusetter == processors)
        cpusetter = 1;
      CPU_SET(cpusetter, &(opt->cpuset));
      cpusetter++;

      D("Tuning buffer thread %i to processor %i", i, cpusetter);
      err =
        pthread_setaffinity_np(opt->rbuf_pthreads[i], sizeof(cpu_set_t),
                               &(opt->cpuset));
      if (err != 0)
        {
          perror("Affinity");
          E("Error: setting affinity");
        }
      CPU_ZERO(&(opt->cpuset));
#endif //TUNE_AFFINITY
      D("Pthread number %d got id %lu", i, opt->rbuf_pthreads[i]);
    }
  return 0;
}

int close_rbufs(struct opt_s *opt, struct stats *da_stats)
{
  int i, err, retval = 0;
  // Stop the memory threads 
  oper_to_all(opt->membranch, BRANCHOP_STOPANDSIGNAL, NULL);
#ifndef UGLY_FIX_ON_RBUFTHREAD_EXIT
  for (i = 0; i < opt->n_threads; i++)
    {
      err = pthread_join(opt->rbuf_pthreads[i], NULL);
      if (err < 0)
        {
          printf("ERROR; return code from pthread_join() is %d\n", err);
          retval--;
        }
      else
        D("%dth buffer exit OK", i);
    }
#endif //UGLY_FIX_ON_RBUFTHREAD_EXIT
  free(opt->rbuf_pthreads);
  D("Getting stats and closing for membranch");
  oper_to_all(opt->membranch, BRANCHOP_CLOSERBUF, (void *)da_stats);

  free(opt->membranch);
  free(opt->bes);

  return retval;
}

int close_opts(struct opt_s *opt)
{
  //int i;
  if (opt->first_packet != NULL)
    free(opt->first_packet);
  if (opt->resqut != NULL)
    free(opt->resqut);
  if (opt->address_to_bind_to != NULL)
    free(opt->address_to_bind_to);
  if (opt->cfgfile != NULL)
    {
      free(opt->cfgfile);
    }
  if (opt->disk2fileoutput != NULL)
    free(opt->disk2fileoutput);
  if (opt->streamer_ent != NULL)
    free(opt->streamer_ent);
  if (opt->hostname != NULL)
    free(opt->hostname);
  if (opt->filename != NULL)
    free(opt->filename);
  if (opt->optbits & WRITE_TO_SINGLE_FILE)
    {
      if (opt->writequeue != NULL)
        {
          pthread_mutex_destroy(opt->writequeue);
          free(opt->writequeue);
        }
      if (opt->writequeue_signal != NULL)
        {
          pthread_cond_destroy(opt->writequeue_signal);
          free(opt->writequeue_signal);
        }
      if (opt->singlefile_fd > 0)
        close(opt->singlefile_fd);
    }
#if(PROTECT_STATUS_W_RWLOCK)
  pthread_rwlock_destroy(&(opt->statuslock));
#endif
  config_destroy(&(opt->cfg));
  /*
     if(opt->total_packets != NULL)
     free(opt->total_packets);
   */
  free(opt);
  return 0;
}

int prep_streamer(struct opt_s *opt)
{
  int err = 0;
  D("Initializing streamer thread");
  /* Format the capturing thread */
  opt->streamer_ent =
    (struct streamer_entity *)malloc(sizeof(struct streamer_entity));
  switch (opt->optbits & LOCKER_CAPTURE)
    {
    case CAPTURE_W_UDPSTREAM:
      if (opt->optbits & READMODE)
        err = udps_init_udp_sender(opt, opt->streamer_ent);
      else
        err = udps_init_udp_receiver(opt, opt->streamer_ent);
      break;
    case CAPTURE_W_FANOUT:
      err = fanout_init_fanout(opt, opt->streamer_ent);
      break;
      /*
         case CAPTURE_W_SPLICER:
         err = sendfile_init_writer(&opt, &(streamer_ent));
         break;
       */
    case CAPTURE_W_DUMMY:
      if (opt->optbits & READMODE)
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
    case CAPTURE_W_TCPSTREAM:
      err = tcp_init(opt, opt->streamer_ent);
      break;
    case CAPTURE_W_MULTISTREAM:
      err = tcp_init(opt, opt->streamer_ent);
      break;
    case CAPTURE_W_TCPSPLICE:
      err = tcp_init(opt, opt->streamer_ent);
      break;
    default:
      LOG("ERROR: Missing capture bit or two set! %lX\n", opt->optbits);
      break;

    }
  if (err != 0)
    {
      LOGERR("Error in thread init\n");
      free(opt->streamer_ent);
      opt->streamer_ent = NULL;
      return -1;
    }
#if(DAEMON)
  opt->get_stats = udpstreamer_stats;
#endif
  return 0;
}

int init_recp(struct opt_s *opt)
{
  int err, i;
  opt->recs =
    (struct recording_entity *)malloc(sizeof(struct recording_entity) *
                                      opt->n_drives);
  CHECK_ERR_NONNULL(opt->recs, "rec entity malloc");
  for (i = 0; i < opt->n_drives; i++)
    {
      /*
       * NOTE: AIOW-stuff and udp-streamer are bidirectional and
       * only require the setting of opt->read to one for 
       * sending stuff
       */
      switch (opt->optbits & LOCKER_REC)
        {
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
        case REC_SENDFILE:
          err = init_sendfile_writer(opt, &(opt->recs[i]));
          break;
        default:
          E("Unknown recorder");
          err = -1;
          break;
        }
      if (err != 0)
        {
          LOGERR("Error in writer init\n");
          /* TODO: Need to free all kinds of stuff if init goes bad */
          /* in the writer itself                                   */
          //free(re);
          //exit(-1);
        }
      /* Add the recording entity to the diskbranch */
    }
  switch (opt->optbits & LOCKER_REC)
    {
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

int close_recp(struct opt_s *opt, struct stats *da_stats)
{
  oper_to_all(opt->diskbranch, BRANCHOP_CLOSEWRITER, (void *)da_stats);
  free(opt->diskbranch);
  free(opt->recs);
  return 0;
}

#if(DAEMON)
void *vlbistreamer(void *opti)
#else
int main(int argc, char **argv)
#endif
{
  int err = 0;
  pthread_t streamer_pthread;

#if(DAEMON)
  struct opt_s *opt = (struct opt_s *)opti;
  D("Starting thread from daemon");
#else
  LOG("Running in non-daemon mode\n");
  struct opt_s *opt = malloc(sizeof(struct opt_s));
  CHECK_ERR_NONNULL(opt, "opt malloc");
  LOG("STREAMER: Reading parameters\n");
  clear_and_default(opt, 1);
  err = parse_options(argc, argv, opt);
  if (err != 0)
    STREAMER_ERROR_EXIT;

  err = init_branches(opt);
  CHECK_ERR("init branches");
  err = init_recp(opt);
  CHECK_ERR("init recpoints");

#ifdef HAVE_LIBCONFIG_H
  err = init_cfg(opt);
  //TODO: cfg destruction
  if (err != 0)
    {
      E("Error in cfg init");
      STREAMER_ERROR_EXIT;
    }
#endif //HAVE_LIBCONFIG_H

  err = init_rbufs(opt);
  CHECK_ERR("init rbufs");
#endif //DAEMON
  /* Check and set cfgs at this point */
  //return -1;

  /* If we're sending stuff, check all the diskbranch members for the files they have   */
  /* Also updates the fileholders list to point the owners of files to correct drives   */
#ifdef HAVE_LIBCONFIG_H
  if (opt->optbits & READMODE)
    {
      oper_to_all(opt->diskbranch, BRANCHOP_CHECK_FILES, (void *)opt);
      LOG
        ("For recording %s: %lu files were found out of %lu total. file index shows %ld files\n",
         opt->filename, opt->cumul_found, opt->cumul, afi_get_n_files(opt->fi));
    }
#endif //HAVE_LIBCONFIG_H

  /* Handle hostname etc */
  /* TODO: Whats the best way that accepts any format? */

  err = prep_streamer(opt);
  if (err != 0)
    {
      STREAMER_ERROR_EXIT;
    }

  if (opt->optbits & READMODE)
    LOG("STREAMER: In main, starting sending thread \n");
  else
    LOG("STREAMER: In main, starting receiver thread \n");

#ifdef HAVE_LRT
  /*  TCP streams start counting from accept and others from here       */
  if (!(opt->optbits & (CAPTURE_W_TCPSPLICE | CAPTURE_W_TCPSTREAM)))
    GETTIME(opt->starting_time);

  set_status_for_opt(opt, STATUS_RUNNING);
  err =
    pthread_create(&streamer_pthread, NULL, opt->streamer_ent->start,
                   (void *)opt->streamer_ent);

  if (err != 0)
    {
      printf("ERROR; return code from pthread_create() is %d\n", err);
      STREAMER_ERROR_EXIT;
    }

  /* Other thread spawned so we can minimize our priority       */
  minimize_priority();

#ifdef TUNE_AFFINITY
  /* Caused some weird ass bugs and crashes so not used anymore NEVER   */
  /* Put the capture on the first core                                  */
  CPU_SET(0, &(opt->cpuset));
  err = pthread_setaffinity_np(streamer_pthread, sizeof(cpu_set_t), &cpuset);
  if (err != 0)
    {
      E("Error: setting affinity: %d", err);
    }
  CPU_ZERO(&cpuset);
#endif

#endif
  {
    /* READMODE shuts itself down so we just go to pthread_join                 */
    /* Check also that last_packet is 0. Else the thread should shut itself     */
    /* down                                                                     */
    if (!(opt->optbits & READMODE) && opt->last_packet == 0 &&
        !(opt->
          optbits & (CAPTURE_W_TCPSPLICE | CAPTURE_W_TCPSTREAM |
                     CAPTURE_W_MULTISTREAM)))
      {
        TIMERTYPE now;
        GETTIME(now);
        while ((GETSECONDS(now) <=
                (GETSECONDS(opt->starting_time) + (long)opt->time)) &&
               get_status_from_opt(opt) == STATUS_RUNNING)
          {
            sleep(1);
            GETTIME(now);
          }
        shutdown_thread(opt);
        pthread_mutex_lock(&(opt->membranch->branchlock));
        pthread_cond_broadcast(&(opt->membranch->busysignal));
        pthread_mutex_unlock(&(opt->membranch->branchlock));
      }
  }
  err = pthread_join(streamer_pthread, NULL);
  if (err < 0)
    {
      printf("ERROR; return code from pthread_join() is %d\n", err);
    }
  else
    D("Streamer thread exit OK");
  LOG("STREAMER: Threads finished. Getting stats for %s\n", opt->filename);
  GETTIME(opt->endtime);

  LOG("Blocking until owned buffers are released\n");
  block_until_free(opt->membranch, opt);
#if(DAEMON)
  LOG("Buffers finished\n");
  if (get_status_from_opt(opt) != STATUS_STOPPED)
    {
      E("Thread didnt finish nicely with STATUS_STOPPED");
      set_status_for_opt(opt, STATUS_FINISHED);
    }
  else
    set_status_for_opt(opt, STATUS_FINISHED);
#else
  close_streamer(opt);
#endif

#if(DAEMON)
  D("Streamer thread exiting for %s", opt->filename);
  pthread_exit(NULL);
#else
  close_opts(opt);
  STREAMER_EXIT;
#endif
}

int close_streamer(struct opt_s *opt)
{
  D("Closing streamer");
  struct stats *stats_full = (struct stats *)malloc(sizeof(struct stats));
  init_stats(stats_full);
  D("stats ready");
  if (opt->streamer_ent != NULL)
    opt->streamer_ent->close(opt->streamer_ent, (void *)stats_full);
  D("Closed streamer_ent");
#if(!DAEMON)
  close_rbufs(opt, stats_full);
  close_recp(opt, stats_full);
  D("Membranch and diskbranch shut down");
#else
  stats_full->total_written = opt->bytes_exchanged;
#endif
  D("Printing stats");
  print_stats(stats_full, opt);
  free(stats_full);
  D("Stats over");

  return 0;
}

/* These two separated here */
int write_cfg(config_t * cfg, char *filename)
{
  int err = config_write_file(cfg, filename);
  if (err == CONFIG_FALSE)
    {
      E("Failed to write CFG to %s", filename);
      return -1;
    }
  else
    return 0;
}

int write_cfg_for_rec(struct opt_s *opt, char *filename)
{
  int err;
  config_t cfg;
  config_init(&cfg);
  stub_rec_cfg(config_root_setting(&cfg), opt);
  err = write_cfg(&cfg, filename);
  config_destroy(&cfg);
  return err;
}

int read_cfg(config_t * cfg, char *filename)
{
  int fd, err;
  int retval = 0;
  /* Need to open for lock ..   */
  err = open(filename, O_RDONLY);
  CHECK_ERR_LTZ("Open read_cfg");
  fd = err;
  err = flock(fd, LOCK_EX);
  CHECK_ERR("lock read_cfg");
  err = config_read_file(cfg, filename);
  if (err == CONFIG_FALSE)
    {
      E("%s:%d - %s", filename, config_error_line(cfg),
        config_error_text(cfg));
      E("Failed to read CFG from %s", filename);
      retval = -1;
    }
  flock(fd, LOCK_UN);
  close(fd);
  return retval;
}

int init_branches(struct opt_s *opt)
{
  int err;
  opt->membranch =
    (struct entity_list_branch *)malloc(sizeof(struct entity_list_branch));
  CHECK_ERR_NONNULL(opt->membranch, "membranch malloc");
  opt->diskbranch =
    (struct entity_list_branch *)malloc(sizeof(struct entity_list_branch));
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

void shutdown_thread(struct opt_s *opt)
{
  if (opt->streamer_ent != NULL && opt->streamer_ent->stop != NULL)
    {
      opt->streamer_ent->stop(opt->streamer_ent);
    }
  else
    E("Cant shutdown streamer_ent with uninitialized stop");
}

#if(DAEMON)
int print_midstats(struct schedule *sched, struct stats *old_stats)
{
  TIMERTYPE temptime;
  float timedif = 0;
  struct listed_entity *le = sched->br.busylist;
  struct stats tempstats;
  float total_mbits = 0;
  GETTIME(temptime);
  timedif = floatdiff(&temptime, &sched->lasttick);
  LOG("Time:\t%lu\n", GETSECONDS(temptime));
  while (le != NULL)
    {
      struct scheduled_event *ev = (struct scheduled_event *)le->entity;
      if (ev->opt->get_stats != NULL)
        {
          memset(&tempstats, 0, sizeof(struct stats));
          ev->opt->get_stats((void *)ev->opt, (void *)&tempstats);
          neg_stats(&tempstats, ev->stats);
          total_mbits = BYTES_TO_MBITSPS(tempstats.total_bytes);
          LOG("Event:\t%s\t", ev->opt->filename);
          LOG("Network:\t%5.0fMb/s\tDropped %lu\tIncomplete %lu",
              total_mbits / timedif, tempstats.dropped, tempstats.incomplete);
          if (tempstats.progress != -1)
            {
              LOG("\tProgress %02d%%", tempstats.progress);
            }
          LOG("\n");
          add_stats(ev->stats, &tempstats);
        }
      le = le->child;
    }
  memset(&tempstats, 0, sizeof(struct stats));
  oper_to_all(sched->default_opt->diskbranch, BRANCHOP_GETSTATS,
              (void *)&tempstats);
  neg_stats(&tempstats, old_stats);
  total_mbits = BYTES_TO_MBITSPS(tempstats.total_written);
  LOG("HD-Speed:\t%5.0fMb/s\n", total_mbits / timedif);
  add_stats(old_stats, &tempstats);

  LOG("Ringbuffers: ");
  print_br_stats(sched->default_opt->membranch);
  LOG("Recpoints: ");
  print_br_stats(sched->default_opt->diskbranch);

  COPYTIME(temptime, sched->lasttick);
  LOG("----------------------------------------\n");
  return 0;
}
#endif

/* Generic function which we use after we get the opt	*/
inline int iden_from_opt(struct opt_s *opt, void *val1, void *val2,
                         int iden_type)
{
  (void)val2;
  switch (iden_type)
    {
    case CHECK_BY_NAME:
      if (strcmp(opt->filename, (char *)val1) == 0)
        return 1;
      else
        return 0;
      break;
    case CHECK_BY_OPTPOINTER:
      if (opt == (struct opt_s *)val1)
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

#if(PPRIORITY)

/*
 *  http://stackoverflow.com/questions/902539/nice-level-for-pthreads
 */
int minimize_priority()
{
  pid_t tid = syscall(SYS_gettid);
  D("Setting tid %d prio from %d to %d", tid, getpriority(PRIO_PROCESS, tid),
    MIN_PRIO_FOR_UNIMPORTANT);
  /* manpage said to clear this */
  errno = 0;
  if ((setpriority(PRIO_PROCESS, tid, MIN_PRIO_FOR_UNIMPORTANT)) != 0)
    E("Setpriority");
  return 0;
}
#endif
inline int get_status_from_opt(struct opt_s *opt)
{
  int returnable;
#if(PROTECT_STATUS_W_RWLOCK)
  pthread_rwlock_rdlock(&(opt->statuslock));
#endif
  returnable = opt->status;
#if(PROTECT_STATUS_W_RWLOCK)
  pthread_rwlock_unlock(&(opt->statuslock));
#endif
  return returnable;
}

void set_status_for_opt(struct opt_s *opt, int status)
{
#if(PROTECT_STATUS_W_RWLOCK)
  pthread_rwlock_wrlock(&(opt->statuslock));
#endif
  opt->status = status;
#if(PROTECT_STATUS_W_RWLOCK)
  pthread_rwlock_unlock(&(opt->statuslock));
#endif
}
