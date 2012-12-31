/*
 * simplebuffer.c -- A simple buffer implementation for vlbi-streamer
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
#include <stdio.h>
#include <malloc.h>
#include <sys/uio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include "config.h"
#include <string.h> /* For preheat */
#if(HAVE_HUGEPAGES)
#include <sys/mman.h>
#endif
#include "resourcetree.h"
#include "simplebuffer.h"
#include "streamer.h"
#include "assert.h"
#include "common_wrt.h"
#include "active_file_index.h"
#ifndef MMAP_NOT_SHMGET
#include <sys/ipc.h>
#include <sys/shm.h>
#endif

extern FILE* logfile;

#define HAVE_ASSERT 1
#define ASSERT(x) do{if(HAVE_ASSERT){assert(x);}}while(0)
#define CHECK_RECER do{if(ret!=0){if(be->recer != NULL){close_recer(be,ret);}return -1;}}while(0)
#define UGLY_TIMEOUT_FIX
//#define DO_W_STUFF_IN_FIXED_BLOCKS

int sbuf_check(struct buffer_entity *be, int tout)
{
  int ret = 0;
  struct simplebuf * sbuf = (struct simplebuf * )be->opt;
  //while ((ret = be->recer->check(be->recer))>0){
  DD("Checking for ready writes. asyndiff: %ld bytes, diff: %ld bytes",,sbuf->asyncdiff,sbuf->diff);
  /* Still doesn't really wait DURR */
  ret = be->recer->check(be->recer, 0);
  if(ret > 0){
    /* Write done so decrement async_writes_submitted */
    //sbuf->async_writes_submitted--;
    D("%d byte Writes complete on seqnum %lu",, ret, sbuf->fileid);
    //unsigned long num_written;

    //num_written = ret/sbuf->opt->packet_size;

    sbuf->asyncdiff-=ret;
  }
  else if (ret == AIO_END_OF_FILE){
    //D("End of file on id %lu",, sbuf->file_seqnum);
    D("End of file on id %lu",, sbuf->fileid);
    sbuf->asyncdiff = 0;
  }
  else if (ret == 0){
    DD("No writes to report on %lu",, sbuf->fileid);
#ifdef UGLY_TIMEOUT_FIX
    if(tout == 1)
      usleep(1000);
#endif
  }
  else{
    E("Error in write check on seqdum %lu",, sbuf->fileid);
    return -1;
  }
  return 0;
}
int sbuf_acquire(void* buffo, void *opti,void* acq)
{
  struct buffer_entity * be = (struct buffer_entity*)buffo;
  struct simplebuf * sbuf = (struct simplebuf *)be->opt;
  sbuf->opt = (struct opt_s *)opti;
  if(sbuf->opt->optbits & USE_RX_RING){
    struct rxring_request* rxr = (struct rxring_request*)acq;
    /* This threads responsible area */
    //sbuf->buffer = sbuf->opt->buffer + ((long unsigned)rxr->bufnum)*(sbuf->opt->packet_size*sbuf->opt->buf_num_elems);
    sbuf->buffer = sbuf->opt->buffer + FILESIZE*((long unsigned)rxr->bufnum);
  }

  sbuf->bufoffset = sbuf->buffer;

  sbuf->fi = sbuf->opt->fi;
  sbuf->fileid = *((long unsigned*)acq);
  sbuf->diff = 0;

  return 0;
}
int sbuf_release(void* buffo)
{
  struct buffer_entity * be = (struct buffer_entity*)buffo;
  struct simplebuf * sbuf = (struct simplebuf *)be->opt;
  if(sbuf->opt->optbits & USE_RX_RING)
    sbuf->buffer = NULL;
  sbuf->opt = NULL;

  //sbuf->opt_old = sbuf->opt;
  //sbuf->file_seqnum_old = sbuf->file_seqnum;
  

  //sbuf->opt = sbuf->opt_default;
  //sbuf->file_seqnum = -1;
  return 0;
}
void preheat_buffer(void* buf, struct opt_s* opt)
{
  //memset(buf, 0, opt->packet_size*(opt->buf_num_elems));
  (void)opt;
  memset(buf, 0, FILESIZE);
}
int sbuf_free(void* buffo)
{
  /*
  if(buffo != NULL){
    struct buffer_entity * be = (struct buffer_entity*)buffo;
    free(be);
  }
  */
  (void)buffo;
  return 0;
}
/* Note as opt goes to null after relase, lingering will not be found 	*/
/* TODO implement lingering-stuff later					*/
int sbuf_identify(void* ent, void* val1, void* val2,int iden_type)
{
  struct buffer_entity *be = (struct buffer_entity*)ent;
  struct simplebuf* sbuf= (struct simplebuf*)be->opt;
  struct opt_s * opt = (struct opt_s*)val2;
  if(iden_type == CHECK_BY_SEQ){
    if(opt == sbuf->opt){
    //if(strcmp(((struct opt_s*)val2)->filename,sbuf->filename_old) == 0){
      if(sbuf->fileid == *((unsigned long*)val1)){
	D("Match!");
	return 1;
      }
    }
    return 0;
  }
  /* Check if we previously had a needed file	*/
  else if (iden_type == CHECK_BY_OLDSEQ){
    if(sbuf->fi == opt->fi)
    {
      if(sbuf->fileid == *((unsigned long*)val1))
      {
	D("Match!");
	return 1;
      }
    }
    return 0;
  }
  return iden_from_opt(sbuf->opt, val1, val2, iden_type);
  //return (const char*)sbuf->opt->filename;
}
void* sbuf_getopt(void * sbuffo)
{
  struct buffer_entity * be = (struct buffer_entity*)sbuffo;
  return (void*)(((struct simplebuf*)be->opt)->opt);
}
int sbuf_init(struct opt_s* opt, struct buffer_entity * be)
{
  //Moved buffer init to writer(Choosable by netreader-thread)
  int err;
  struct simplebuf * sbuf = (struct simplebuf*) malloc(sizeof(struct simplebuf));
  if(sbuf == NULL)
    return -1;
  be->opt = sbuf;
  sbuf->opt = opt;
  be->recer =NULL;
  sbuf->bufnum = sbuf->opt->bufculum++;
  sbuf->optbits = sbuf->opt->optbits;

  //sbuf->filename_old = (char*)malloc(sizeof(char)*FILENAME_MAX);
  //sbuf->file_seqnum_old = -1;
  
  sbuf->fi = NULL;
  sbuf->fileid = -1;
  /*
  sbuf->fh_def = (struct fileholder*)malloc(sizeof(struct fileholder));
  sbuf->fh = sbuf->fh_def;
  */

  sbuf->opt_default = opt;

  //be->membranch = opt->membranch;
  //be->diskbranch = opt->diskbranch;
  D("Adding simplebuf to membranch");
  struct listed_entity *le = (struct listed_entity*)malloc(sizeof(struct listed_entity));
  CHECK_ERR_NONNULL(le, "malloc listed_entity");
  le->entity = (void*)be;
  le->child = NULL;
  le->father = NULL;
  le->acquire = sbuf_acquire;
  le->check = NULL; //sbuf_seqnumcheck;
  le->release = sbuf_release;
  le->close = sbuf_free;
  le->identify = sbuf_identify;
  le->getopt = sbuf_getopt;
  be->self = le;
  add_to_entlist(sbuf->opt->membranch, be->self);
  D("Ringbuf added to membranch");

  
  /* Main arg for bidirectionality of the functions 		*/
  //sbuf->read = opt->read;

  //sbuf->opt->optbits = opt->optbits;
  //sbuf->file_seqnum = -1;
  sbuf->async_writes_submitted = 0;
  sbuf->ready_to_act = 0;

  //sbuf->opt->buf_num_elems = opt->buf_num_elems;
  //sbuf->opt->packet_size = opt->packet_size;
  //sbuf->writer_head = 0;
  //sbuf->tail = sbuf->hdwriter_head = (sbuf->opt->buf_num_elems-1);
  sbuf->diff =0;
  sbuf->asyncdiff =0;
  sbuf->running = 0;
  //sbuf->opt->do_w_stuff_every = opt->do_w_stuff_every;

  be->headlock = (LOCKTYPE *)malloc(sizeof(LOCKTYPE));
  CHECK_ERR_NONNULL(be->headlock, "Headlock malloc");
  be->iosignal = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
  CHECK_ERR_NONNULL(be->iosignal, "iosignal malloc");
  err = LOCK_INIT(be->headlock);
  CHECK_ERR("headlock init");
  err = pthread_cond_init(be->iosignal, NULL);
  CHECK_ERR("iosignal init");

  if(!(sbuf->opt->optbits & USE_RX_RING)){
    //unsigned long hog_memory = sbuf->opt->buf_num_elems*sbuf->opt->packet_size;
    unsigned long hog_memory = FILESIZE;
    D("Trying to hog %lu MB of memory",,hog_memory/MEG);
    /* TODO: Make a check for available number of hugepages */
#if(HAVE_HUGEPAGES)
    if(sbuf->optbits & USE_HUGEPAGE){
      /* Init fd for hugetlbfs					*/
      /* HUGETLB not yet supported on mmap so using MAP_PRIVATE	*/

      /*
      char hugefs[FILENAME_MAX];
      find_hugetlbfs(hugefs, FILENAME_MAX);

      sprintf(hugefs, "%s%s%ld", hugefs,"/",pthread_self());
#if(DEBUG_OUTPUT)
      fprintf(stdout, "RINGBUF: Initializing hugetlbfs file as %s\n", hugefs);
#endif

      common_open_file(&(sbuf->huge_fd), O_RDWR,hugefs,0);
      */
      //sbuf->huge_fd = open(hugefs, O_RDWR|O_CREAT, 0755);

      /* TODO: Check other flags aswell				*/
      /* TODO: Not sure if shared needed as threads share id 	*/
      //sbuf->buffer = mmap(NULL, (sbuf->opt->buf_num_elems)*(sbuf->opt->packet_size), PROT_READ|PROT_WRITE , MAP_SHARED|MAP_HUGETLB, sbuf->huge_fd,0);
      //assert(hog_memory%sysconf(_SC_PAGESIZE) == 0);
#ifdef MMAP_NOT_SHMGET
      sbuf->buffer = mmap(NULL, hog_memory, PROT_READ|PROT_WRITE , MAP_ANONYMOUS|MAP_PRIVATE|MAP_HUGETLB|MAP_NORESERVE, -1,0);
      //sbuf->buffer = mmap(NULL, hog_memory, PROT_READ|PROT_WRITE , MAP_ANONYMOUS|MAP_PRIVATE|MAP_NORESERVE, sbuf->huge_fd,0);
      if(sbuf->buffer ==MAP_FAILED){
	perror("MMAP");
	E("Couldn't allocate hugepages");
	//remove(hugefs);
	err = -1;
      }
      else{
	err = 0;
	D("mmapped to hugepages");
      }
#else
      sbuf->shmid = shmget(sbuf->bufnum, hog_memory, IPC_CREAT|IPC_EXCL|SHM_HUGETLB|SHM_NORESERVE|SHM_W|SHM_R);
      if(sbuf->shmid <0){
	E("Shmget failed");
	perror("shmget");
	return -1;
      }
      sbuf->buffer = shmat(sbuf->shmid, NULL, 0);
      if((long)sbuf->buffer == (long)-1){
	E("shmat failed");
	perror("shmat");
	return -1;
      }
#endif

    }
    else
#endif /* HAVE_HUGEPAGES */
    {
      long maxmem = sysconf(_SC_AVPHYS_PAGES)*sysconf(_SC_PAGESIZE);
      if(hog_memory > (long unsigned)maxmem){
	LOG("Max allocatable memory %ld MB. still reserving %lu MB more\n", maxmem/MEG, hog_memory/MEG);
	//return -1;
      }
      D("Memaligning buffer with %i sized %lu n_elements",,sbuf->opt->buf_num_elems, sbuf->opt->packet_size);
      err = posix_memalign((void**)&(sbuf->buffer), sysconf(_SC_PAGESIZE), hog_memory);
      //sbuf->buffer = malloc(((unsigned long)sbuf->opt->buf_num_elems)*((unsigned long)sbuf->opt->packet_size));
      //madvise(sbuf->buffer,   ((unsigned long)sbuf->opt->buf_num_elems)*((unsigned long)sbuf->opt->packet_size),MADV_SEQUENTIAL|MADV_WILLNEED); 
      preheat_buffer(sbuf->buffer, opt);

    }
    if (err < 0 || sbuf->buffer == 0) {
      perror("make_write_buffer");
      return -1;
    }
    D("Memory allocated");
  }
  else
    D("Memmapped in main so not reserving here");
  

  return 0;
}
int sbuf_close(struct buffer_entity* be, void *stats)
{
  (void)stats;

  struct simplebuf * sbuf = (struct simplebuf *) be->opt;
  D("Closing simplebuf for id %d ",, sbuf->bufnum);
  LOCK_DESTROY(be->headlock);
  LOCK_FREE(be->headlock);
  free(be->iosignal);

  //int ret = 0;
  /*
     if(be->recer->close != NULL)
     be->recer->close(be->recer, stats);
     */
  if(!(sbuf->optbits & USE_RX_RING)){
#if(HAVE_HUGEPAGES)
    if(sbuf->optbits & USE_HUGEPAGE){
      //munmap(sbuf->buffer, sbuf->opt->packet_size*sbuf->opt->buf_num_elems);
      munmap(sbuf->buffer, FILESIZE);
    }
    else
#endif /* HAVE_HUGEPAGES */
      free(sbuf->buffer);
  }
  else
    D("Not freeing mem. Done in main");
  D("Freeing structs");
  free(sbuf);
  //free(be);
  //free(be->recer);
  D("Simplebuf closed");
  return 0;
}
void close_recer(struct buffer_entity *be, int errornum)
{
  struct simplebuf *sbuf = (struct simplebuf *)be->opt;
  be->recer->handle_error(be->recer, errornum);
  if(sbuf->opt->optbits & READMODE){
    /*
    remove_specific_from_fileholders(sbuf->opt, sbuf->fh->diskid);
    */
    set_free(sbuf->opt->membranch, be->self);
    sbuf->ready_to_act = 0;
  }
  //sbuf->file_seqnum = -1;
  be->recer = NULL;
}
int simple_end_transaction(struct buffer_entity *be)
{
  struct simplebuf *sbuf = (struct simplebuf*)be->opt;
  //void *offset = sbuf->buffer + (sbuf->opt->buf_num_elems - sbuf->diff)*sbuf->opt->packet_size;
  unsigned long wrote_extra = 0;
  long ret = 0;

  //unsigned long count = sbuf->diff*(sbuf->opt->packet_size);
  unsigned long count = sbuf->diff;
  unsigned long origcount = count;
  //void * start = sbuf->buffer + (*tail * sbuf->opt->packet_size);
  if((wrote_extra = count % BLOCK_ALIGN) != 0){
    wrote_extra = BLOCK_ALIGN -wrote_extra;
    count += wrote_extra;
  }
  /*
  while(count % BLOCK_ALIGN != 0){
    count++;
    wrote_extra++;
  }
    */
  D("Have to write %lu extra bytes so count is %lu",, wrote_extra, count);

  ret = be->recer->write(be->recer, sbuf->bufoffset, count);
  if(ret == EOF)
  {
    D("Got end of file");
    sbuf->diff = 0;
  }
  else if(ret<0){
    E("Error in Rec entity write: %ld. Left to write:%ld bytes",, ret,sbuf->diff);
    close_recer(be,ret);
    return -1;
  }
  else if (ret == 0){
    //Don't increment
  }
  else{
    //if(sbuf->opt->optbits & ASYNC_WRITE)
    //sbuf->async_writes_submitted++;
    if((unsigned long)ret != count){
      E("Write wrote %ld out of %lu",, ret, count);
      /* TODO: Handle incrementing so we won't lose data */
      sbuf->diff -=  ret;
      sbuf->bufoffset += ret;
    }
    else
    {
    //increment_amount(sbuf, &(sbuf->hdwriter_head), endi);
    sbuf->diff -=  origcount;
    sbuf->bufoffset += origcount;
    }
  }
  return 0;

}
void* sbuf_getbuf(struct buffer_entity *be, long ** diff)
{
  struct simplebuf *sbuf = (struct simplebuf*)be->opt;
  *diff = &(sbuf->diff);
  return sbuf->buffer;
}
int simple_write_bytes(struct buffer_entity *be)
{
  struct simplebuf * sbuf = (struct simplebuf *)be->opt;
  long ret;
  //unsigned long limit = sbuf->opt->do_w_stuff_every*(sbuf->opt->packet_size);
#if(WRITE_GRANUALITY)
  unsigned long limit = sbuf->opt->do_w_stuff_every;
#else
  //unsigned long limit = sbuf->opt->buf_num_elems*sbuf->opt->packet_size;
  unsigned long limit = FILESIZE;
#endif

  //unsigned long count = sbuf->diff * sbuf->opt->packet_size;
  unsigned long count = sbuf->diff;
  //void * offset = sbuf->buffer + (sbuf->opt->buf_num_elems - sbuf->diff)*(sbuf->opt->packet_size);
  //void * offset = sbuf->buffer + (sbuf->opt->buf_num_elems - sbuf->diff)*(sbuf->opt->packet_size);
  //ASSERT(sbuf->bufoffset + count <= sbuf->buffer+sbuf->opt->packet_size*sbuf->opt->buf_num_elems);
  ASSERT(sbuf->bufoffset + count <= sbuf->buffer+FILESIZE);
  ASSERT(count != 0);

  if(count > limit){
    count = limit;
  }
  if (limit != count){
    D("Only need to finish transaction. limit %lu, count %lu",, limit, count);
    return simple_end_transaction(be);
  }
  ASSERT(count % 4096 == 0);

  DD("Starting write with count %lu",,count);
  ret = be->recer->write(be->recer, sbuf->bufoffset, count);
  if(ret == EOF)
  {
    D("End of file received");
    sbuf->diff = 0;
  }
  else if(ret<0){
    E("RINGBUF: Error in Rec entity write: %ld. Left to write: %ld bytes offset: %lu count: %lu\n",, ret,sbuf->diff, (unsigned long)sbuf->bufoffset, count);
    close_recer(be,ret);
    return -1;
  }
  else if (ret == 0){
    if(!(sbuf->opt->optbits & READMODE)){
      E("Failed write with 0");
      //close_recer(be,ret);
    }
    //Don't increment
  }
  else{
    if((unsigned long)ret != count){
      E("Write wrote %ld out of %lu\n",, ret, count);
      sbuf->diff -= ret;
      sbuf->bufoffset+=ret;
    }
    else{
#if(WRITE_GRANUALITY)
      //sbuf->diff-=sbuf->opt->do_w_stuff_every/sbuf->opt->packet_size;
      sbuf->diff-=sbuf->opt->do_w_stuff_every;
#else
      sbuf->diff = 0;
#endif
      sbuf->bufoffset += count;
      //if(sbuf->bufoffset >= sbuf->buffer +(sbuf->opt->buf_num_elems*sbuf->opt->packet_size))
      //if(sbuf->bufoffset >= sbuf->buffer +FILESIZE)
	//sbuf->bufoffset = sbuf->buffer;
    }
  }
  return 0;
}
int sbuf_async_loop(struct buffer_entity *be)
{
  struct simplebuf * sbuf = (struct simplebuf *)be->opt;
  int err;
  sbuf->asyncdiff = sbuf->diff;
  while(sbuf->asyncdiff > 0){
    if(sbuf->diff > 0){
      err = simple_write_bytes(be);
      CHECK_ERR_QUIET("Bytes written");
      DD("Checking async");
      err = sbuf_check(be,0);
      CHECK_ERR_QUIET("Async bytes written");
    }
    else if (be->recer == NULL){
      E("Lost recer. restart!");
      return -1;
    }
    else{
      DD("Only checking. Not writing. asyncdiff still %ld bytes",,sbuf->asyncdiff);
      err = sbuf_check(be,1);
      CHECK_ERR_QUIET("Async check");
    }
  }
  return 0;
}
int sbuf_sync_loop(struct buffer_entity *be)
{
  struct simplebuf * sbuf = (struct simplebuf *)be->opt;
  int err;
  DD("Starting write loop for %ld bytes",, sbuf->diff);
  while(sbuf->diff > 0){
    err = simple_write_bytes(be);
    CHECK_ERR_QUIET("sync bytes written");
    DD("Writeloop done. diff:  %ld",,sbuf->diff);
  }
  return 0;
}
int write_buffer(struct buffer_entity *be)
{
  struct simplebuf* sbuf = (struct simplebuf*)be->opt;
  int ret;

  if(be->recer == NULL){
    ret = 0;
    D("Getting rec entity for buffer");
    /* If we're reading, we need a specific recorder_entity */
    if(sbuf->opt->optbits & READMODE){
      //memset(sbuf->filename_old, 0, sizeof(char)*FILENAME_MAX);
      D("Getting rec entity id %d for file %lu",, sbuf->opt->fi->files[sbuf->fileid].diskid, sbuf->fileid);
      be->recer = (struct recording_entity*)get_specific(sbuf->opt->diskbranch, sbuf->opt, sbuf->fileid, sbuf->running, sbuf->opt->fi->files[sbuf->fileid].diskid, &ret);
      /* TODO: This is a real bummer. Handle! 	*/
      if (be->recer == NULL || ret !=0){
	E("Specific writer fails on acquired.");
	E("Shutting it down and removing from list");
	/* Not thread safe atm 			*/
	//Let the one using it close it!
	if(be->recer !=NULL){
	  close_recer(be,ret);
	}
	return -1;
      }
      D("Got rec entity %d to load %s file %lu!",, be->recer->getid(be->recer), sbuf->opt->fi->filename, sbuf->fileid);
      off_t rfilesize = be->recer->get_filesize(be->recer);
      if(sbuf->diff != rfilesize)
      {
	D("Adjusting filesize from %lu to %lu",, sbuf->diff, rfilesize);
	sbuf->diff = rfilesize;
      }
    }
    else{
      be->recer = (struct recording_entity*)get_free(sbuf->opt->diskbranch, sbuf->opt,((void*)&(sbuf->fileid)), &ret);
      if(be->recer == NULL){
	E("Didn't get a recer. This is bad so exiting");
	sbuf->running = 0;
	remove_from_branch(sbuf->opt->membranch, be->self, 0);
	return -1;
      }
      else if(ret !=0){
	E("Error in acquired for random entity");
	E("Shutting faulty writer down");
	/* TODO: In daemonmode, the remove specific needs to exist also! */
	close_recer(be,ret);
	/* The next round will get a new recer */
	return -1;
      }
      else{
	D("Got recer so updating file_index %s on id %lu for in mem and busy ",, sbuf->opt->fi->filename, sbuf->fileid);
	add_file(sbuf->opt->fi, sbuf->fileid, be->recer->getid(be->recer), FH_INMEM|FH_BUSY);
	//update_fileholder(sbuf->opt->fi, sbuf->fileid, FH_INMEM|FH_BUSY, ADDTOFILESTATUS, be->recer->getid(be->recer));
      }
    }
    //CHECK_AND_EXIT(be->recer);
    //D("Got rec entity");
  }

  if(sbuf->opt->optbits & ASYNC_WRITE){
    ret = sbuf_async_loop(be);
  }
  else{
    ret = sbuf_sync_loop(be);
  }
  if(ret != 0){
    E("Faulty recer");
    if(be->recer != NULL)
      close_recer(be,ret);
    return -1;
  }

  /* If we've succesfully written everything: set everything free etc */
  if(sbuf->diff == 0){
    D("Operation complete. Releasing recpoint");
    /* Might have closed recer already 	*/
    if(be->recer != NULL)
      set_free(sbuf->opt->diskbranch, be->recer->self);
    be->recer = NULL;
    D("Recpoint released");
  }
  return ret;
}
void *sbuf_simple_write_loop(void *buffo)
{
  D("Starting simple write loop");
  struct buffer_entity * be = (struct buffer_entity *)buffo;
  struct simplebuf * sbuf = (struct simplebuf *)be->opt;
  int ret=0;
  int savedif=0;
  sbuf->running = 1;
  D("Start running");
  while(sbuf->running == 1){
    /* Checks if we've finished a write and we're holding a writer 	*/
    /* In this case we need to free the writer and ourselves		*/
    while(sbuf->ready_to_act == 0 && sbuf->running == 1){
      LOCK(be->headlock);
      D("Sleeping on ready");
      pthread_cond_wait(be->iosignal, be->headlock);
      D("Woke up");
      UNLOCK(be->headlock);
    }

    if(sbuf->diff > 0){
      D("Blocking reads/writes. Left to read/write %ld for file %lu",,sbuf->diff,sbuf->fileid);
      assert(sbuf->diff <= FILESIZE);
      savedif = sbuf->diff;
      ret = -1;

      //while(ret!= 0 && sbuf->running == 1){
      while(ret!= 0 && sbuf->running == 1){
	/* Write failed so set the diff back to old value and rewrite	*/
	ret = write_buffer(be);
	if(ret != 0){
	  D("Error in rec. Returning diff and bufoffset");
	  sbuf->diff = savedif;
	  sbuf->bufoffset = sbuf->buffer;
	  if(sbuf->opt->optbits & READMODE){
	    D("Error on drive with readmode on. File is skipped and set self to free");
	    //Common_write handles the fileholder-settings
	    sbuf->ready_to_act = 0;
	    set_free(sbuf->opt->membranch, be->self);
	    ret=0;
	    sbuf->diff = 0;
	  }
	}
	/*If everything went great */
	else{
	  D("Write/read complete!");
	  sbuf->ready_to_act = 0;

	  if(sbuf->opt->optbits & READMODE){
	    update_fileholder_status(sbuf->opt->fi, sbuf->fileid, FH_INMEM, ADDTOFILESTATUS);
	    D("Read cycle complete. Setting self to loaded with %lu",, sbuf->fileid);
	    set_loaded(sbuf->opt->membranch, be->self);
	  }
	  else{
	    update_fileholder_status(sbuf->opt->fi, sbuf->fileid, FH_BUSY, DELFROMFILESTATUS);
	    update_fileholder_status(sbuf->opt->fi, sbuf->fileid, FH_ONDISK, ADDTOFILESTATUS);
	    D("Write cycle complete. Setting self to free");
	    set_free(sbuf->opt->membranch, be->self);
	  }
	}
      }
    }
  }
  D("Finished on id %d",,sbuf->bufnum);
  pthread_exit(NULL);
}
void sbuf_cancel_writebuf(struct buffer_entity *be)
{
  D("Cancelling request for buffer");
  struct simplebuf *sbuf = be->opt;
  sbuf->ready_to_act = 0;
  set_free(sbuf->opt->membranch, be->self);
}
void sbuf_stop_running(struct buffer_entity *be)
{
  D("Stopping sbuf thread");
  ((struct simplebuf*)be->opt)->running = 0;
  LOCK(be->headlock);
  pthread_cond_signal(be->iosignal);
  UNLOCK(be->headlock);
  D("Stopped and signalled");
}
void sbuf_set_ready(struct buffer_entity *be)
{
  ((struct simplebuf*)be->opt)->ready_to_act = 1;
}
int sbuf_init_buf_entity(struct opt_s * opt, struct buffer_entity *be)
{
  be->init = sbuf_init;
  //be->write = sbuf_aio_write;
  //be->get_writebuf = sbuf_get_buf_to_write;
  be->simple_get_writebuf = sbuf_getbuf;
  //be->get_inc = sbuf_getinc;
  //be->wait = sbuf_wait;
  be->close = sbuf_close;
  be->write_loop = sbuf_simple_write_loop;
  be->stop = sbuf_stop_running;
  be->set_ready = sbuf_set_ready;
  be->acquire = sbuf_acquire;
  be->cancel_writebuf = sbuf_cancel_writebuf;

  return be->init(opt,be); 
}
