/*
 * common_wrt.c -- Common IO functions for vlbi-streamer
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
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <string.h>
#include <dirent.h> /* check dir */
#include <regex.h> /* For regexp file matching */
#include "config.h"

#include "streamer.h"
//#include "resourcetree.h"
#include "common_wrt.h"
#include "confighelper.h"
#include "active_file_index.h"
#define ERR_IN_INIT free(dirname);return err 

extern FILE* logfile;

//int common_open_new_file(void * recco, void *opti,unsigned long seq, unsigned long sbuf_still_running){
int common_getid(struct recording_entity*re){
  return ((struct common_io_info*)re->opt)->id;
}
int common_open_new_file(void * recco, void *opti,void* acq){
  int err;
  int tempflags;
  struct recording_entity * re = (struct recording_entity*)recco;
  struct common_io_info * ioi = (struct common_io_info*)re->opt;
  D("Acquiring new recorder and changing opt");
  ioi->opt = (struct opt_s*)opti;

  if(acq != NULL)
    ioi->file_seqnum = *((unsigned long*)acq);
  else
  {
    E("Getting free, but not setting file_seqnum. Something weird happending");
  }
  if(ioi->opt->optbits & WRITE_TO_SINGLE_FILE)
  {
    sprintf(ioi->curfilename, "%s%i%s%s%s%s", ROOTDIRS, ioi->id, "/",ioi->opt->filename, "/",ioi->opt->filename); 
    D("Ready to read/write %ld from/to continuous file %s",, ioi->file_seqnum, ioi->curfilename);
  }
  else{
    sprintf(ioi->curfilename, "%s%i%s%s%s%s.%08ld", ROOTDIRS, ioi->id, "/",ioi->opt->filename, "/",ioi->opt->filename,ioi->file_seqnum); 
  }

  if(ioi->opt->optbits & READMODE)
    tempflags = re->get_r_flags();
  else
    tempflags = re->get_w_flags();


  D("Opening file %s",,ioi->curfilename);

  if(ioi->opt->optbits & WRITE_TO_SINGLE_FILE && ioi->opt->singlefile_fd != 0){
    ioi->fd = ioi->opt->singlefile_fd;
    D("Copying old fd %d for use",, ioi->fd);
  }
  else
  {
    err = common_open_file(&(ioi->fd),tempflags, ioi->curfilename, 0);
    if(err!=0){
      /* Situation where the directory doesn't yet exist but we're 	*/
      /* writing so this shouldn't be a failure 				*/
      if((err & (ENOTDIR|ENOENT)) && !(ioi->opt->optbits & READMODE)){
	D("Directory doesn't exist. Creating it");
	err = init_directory(re);
	CHECK_ERR("Init directory");
	err = common_open_file(&(ioi->fd),tempflags, ioi->curfilename, 0);
	if(err!=0){
	  fprintf(stderr, "COMMON_WRT: Init: Error in file open: %s\n", ioi->curfilename);
	  return err;
	}
      }
      else{
	fprintf(stderr, "COMMON_WRT: Init: Error in file open: %s\n", ioi->curfilename);
	return err;
      }
    }
    if(ioi->opt->optbits & WRITE_TO_SINGLE_FILE){
      ioi->opt->singlefile_fd = ioi->fd;
      if(ioi->opt->optbits & READMODE)
	ioi->filesize = (ioi->opt->buf_num_elems * (ioi->opt->packet_size - ioi->opt->offset_onwrite));
      else
	ioi->filesize = (ioi->opt->buf_num_elems * (ioi->opt->packet_size - ioi->opt->offset));
    }
    else if(ioi->opt->optbits & READMODE)
    {
      struct stat statinfo;
      err = stat(ioi->curfilename, &statinfo);
      ioi->filesize = statinfo.st_size;
      D("Filesize is %lu",, ioi->filesize);
    }
  }

  /* Doh this might be used by other write methods aswell */
  if(ioi->opt->optbits & WRITE_TO_SINGLE_FILE)
  {
    ioi->offset = (CALC_BUFSIZE_FROM_OPT(ioi->opt))*ioi->file_seqnum;
    D("Continous file, so offset set to %ld",, ioi->offset);
  }
  else
    ioi->offset = 0;

  return 0;
}
int common_finish_file(void *recco){
  struct recording_entity * re = (struct recording_entity*)recco;
  struct common_io_info * ioi = (struct common_io_info*)re->opt;
  int ret=0;
  if(ioi->opt->optbits & WRITE_TO_SINGLE_FILE)
  {
    pthread_mutex_lock(ioi->opt->writequeue);
    ioi->opt->next_fd_id_to_write++;
    D("Increased filenum to %ld",, ioi->opt->next_fd_id_to_write);
    pthread_cond_broadcast(ioi->opt->writequeue_signal);
    pthread_mutex_unlock(ioi->opt->writequeue);
  }
  else
  {
    ret = close(ioi->fd);
    if(ret != 0)
      E("Error in closing file!");
    else
      D("File closed");
  }
  if(ioi->opt->optbits & USE_HUGEPAGE)
  {
    if (lseek(ioi->shmid, 0, SEEK_SET) != 0)
      E("Error in seeking shmid back to 0");
  }
  memset(ioi->curfilename, 0, sizeof(char)*FILENAME_MAX);
  ioi->opt = NULL;
  return ret;
}
int common_open_file(int *fd, int flags, char * filename, loff_t fallosize){
  struct stat statinfo;
  int err =0;

  //ioi->latest_write_num = 0;
  //Check if file exists
  //ioi->f_flags = O_WRONLY|O_DIRECT|O_NOATIME|O_NONBLOCK;
  //ioi->filename = opt->filenames[opt->taken_rpoints++];
  err = stat(filename, &statinfo);
  if (err < 0) {
    if (errno == ENOENT){
      //We're reading the file
      /* Goddang what's the big idea setting O_RDONLY to 00 */
      if(!(flags & (O_WRONLY|O_RDWR))){
	perror("File open");
	E("File %s not found, eventhought we wan't to read",,filename);
	return EACCES;
      }
      else{
	D("COMMON_WRT: File doesn't exist. Creating it");
	flags |= O_CREAT;
	err = 0;
      }
    }
    /* Directory doesn't exist */
    else if(errno == ENOTDIR){
      E("The directory doesn't exist");
      return ENOTDIR;
    }
    else{
      fprintf(stderr,"Error: %s on %s\n",strerror(errno), filename);
      return err;
    }
  }

  //This will overwrite existing file.TODO: Check what is the desired default behaviour 
  D("COMMON_WRT: Opening file %s",, filename);
  *fd = open(filename, flags, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
  if(*fd < 0){
    if(flags & O_WRONLY){
      /* Silent version since, acquire stumbles here and it  	*/
      /* Works as planned. Could do string processing		*/
      /* to check the dir etc. but meh				*/
      D("File opening failed with write on for %s. Acquire should create dir",,filename);
      return *fd;
    }
    fprintf(stderr,"Error: %s on %s\n",strerror(errno), filename);
    return *fd;
  }
  D(" File opened as fd %d",, *fd);
  if(fallosize > 0){
    err = fallocate(*fd, 0,0, fallosize);
    if(err < 0){
      perror("fallocate");
      if(errno == EOPNOTSUPP){
	fprintf(stdout, "COMMON_WRT: Not fallocating, since not supported\n");
	err = 0;
      }
      else{
	fprintf(stderr, "COMMON_WRT: Fallocate failed on %s\n", filename);
	return err;
      }
    }
    D("File preallocated");
  }
  else
    D("Not fallocating");
  return err;
}
int common_writecfg(struct recording_entity *re, void *opti){
  int err;
  struct common_io_info *ioi  = re->opt;
  struct opt_s * opt = opti;
  char * cfgname = (char*) malloc(sizeof(char)*FILENAME_MAX);
  char * dirname = (char*) malloc(sizeof(char)*FILENAME_MAX);
  CHECK_ERR_NONNULL(cfgname, "Malloc cfgname");
  CHECK_ERR_NONNULL(dirname, "Malloc dirname");
  struct stat sb;

  sprintf(dirname, "%s%i%s%s", ROOTDIRS, ioi->id, "/",opt->filename); 

  if(stat(dirname,&sb) != 0){
    //perror("Error Opening dir");
    //ERR_IN_INIT;
    D("The dir %s doesn't exist. Probably no files written to it. Returning ok",, dirname);
    free(cfgname);
    free(dirname);
    return 0;
  }

  sprintf(cfgname, "%s%i%s%s%s%s%s", ROOTDIRS, ioi->id, "/",opt->filename, "/",opt->filename, ".cfg"); 
  err = write_cfg_for_rec(opt, cfgname);
  CHECK_ERR("write cfg for rec");
  free(cfgname);
  free(dirname);

  return err;
}
int common_readcfg(struct recording_entity *re, void *opti){
  int err;
  struct common_io_info *ioi  = re->opt;
  struct opt_s * opt = opti;
  char * cfgname = (char*) malloc(sizeof(char)*FILENAME_MAX);
  CHECK_ERR_NONNULL(cfgname,"cfgname malloc");

  sprintf(cfgname, "%s%i%s%s%s%s%s", ROOTDIRS, ioi->id, "/",opt->filename, "/",opt->filename, ".cfg"); 
  err = read_cfg(&(opt->cfg), cfgname);
  free(cfgname);

  return err;
}
int handle_error(struct recording_entity *re, int errornum){
  int err = 0;
  struct common_io_info * ioi = (struct common_io_info*)re->opt;
  if(errornum == ENOSPC){
    E("Mount point full. Setting to read only");
    //TODO: Augment infra for read only nodes
    //Remember to not remove straight from branch
    ioi->status = RECSTATUS_FULL;
    set_free(ioi->opt->diskbranch, re->self);
  }
  else
  {
    E("Writer broken");
    /* Done in close */
    ioi->opt->hd_failures++;
    if(ioi->opt->optbits & READMODE){
      remove_specific_from_fileholders(ioi->opt->filename, ioi->id);
    }
    remove_from_branch(ioi->opt->diskbranch, re->self,0);
    err = re->close(re, NULL);
    CHECK_ERR("Close faulty recer");
    /* TODO:  Need to solve this free-stuff! */
    //free(re);
    //be->recer->close(be->recer,NULL);
    D("Closed recer");
  }

  return err;
}
void* recpoint_getopt(void* red)
{
  struct recording_entity* re = (struct recording_entity*) red;
  return (void*)(((struct common_io_info*)(re->opt))->opt);
}
int common_close_and_free(void* recco){
  int err;
  struct recording_entity* re = (struct recording_entity*)recco;
  err = pthread_mutex_destroy(&(re->self->waitlock));
  if(err != 0)
    E("Error in mutex destroy of waitlock");
  err = pthread_cond_destroy(&(re->self->waitsig));
  if(err != 0)
    E("Error in waitsig destroy");
  return 0;
}
int init_directory(struct recording_entity *re){
  int err;
  struct common_io_info *ioi = (struct common_io_info*)re->opt;
  char * dirname = (char*)malloc(sizeof(char)*FILENAME_MAX);
  /* Create directory */

  sprintf(dirname, "%s%i%s%s", ROOTDIRS, ioi->id, "/",ioi->opt->filename);
  D("Creating directory %s",, dirname);
  if(ioi->opt->optbits & READMODE){
    struct stat sb;

    if((err = stat(dirname,&sb)) != 0){
      perror("Error Opening dir");
      ERR_IN_INIT;
    }
    else if (!S_ISDIR(sb.st_mode)){
      E("%s Not a directory. Not initializing this reader",,dirname);
      free(dirname);
      return -1;
      //ERR_IN_INIT;
    }
  }
  else{
    mode_t process_mask = umask(0);
    err = mkdir(dirname, 0777);
    umask(process_mask);
    if(err!=0){
      if(errno == EEXIST){
	/* Directory exists shouldn't be ok in final release, since 	*/
	/* it will overwrite existing recordings			*/
	D("Directory exist. OK!");
      }
      else{
	E("COMMON_WRT: Init: Error in file open: %s",, dirname);
	free(dirname);
	return errno;
	//ERR_IN_INIT;
      }
    }
  }
  free(dirname);
  return 0;
}
int common_identify(void * ent, void* val1, void* val2, int iden_type){
  struct recording_entity *re = (struct recording_entity *)ent;
  struct common_io_info * ioi = (struct common_io_info *)re->opt;
  /* Special check for recpoint */
  if(iden_type == CHECK_BY_SEQ){
    if(ioi->id == (int)*((unsigned long*)val1))
      return 1;
    else
      return 0;
  }
  return iden_from_opt(ioi->opt, val1, val2, iden_type);
}
void common_infostring(void * le, char* returnable){
  struct recording_entity * re = (struct recording_entity*)le;
  struct common_io_info * ioi = (struct common_io_info*)re->opt;
  sprintf(returnable, "%s%i%s%s%s%lu", "ID: ", ioi->id, " Filename: ", ioi->curfilename, " File seqnum: ", ioi->file_seqnum);
}
int check_if_suitable(void * le, void* other)
{
  struct opt_s* opt_other = (struct opt_s*)other;
  struct common_io_info* ioi = (struct common_io_info*)(((struct recording_entity*)le)->opt);

  if(ioi->status & RECSTATUS_ERROR){
    D("Not returning faulty drive");
    return -1;
  }
  if(!(opt_other->optbits & READMODE) && ioi->status & RECSTATUS_FULL)
  {
    D("Not returning full or erronous entity");
    return -1;
  }
  return 0;
}
int common_w_init(struct opt_s* opt, struct recording_entity *re){
  int err;
  //void * errpoint;
  re->opt = (void*)malloc(sizeof(struct common_io_info));
  re->get_stats = get_io_stats;
  struct common_io_info * ioi = (struct common_io_info *) re->opt;
  CHECK_ERR_NONNULL_AUTO(ioi);
  memset(ioi, 0, sizeof(struct common_io_info));
  //loff_t prealloc_bytes;
  //struct stat statinfo;
  //int err =0;
  //ioi->opt->optbits = opt->optbits;
  ioi->opt = opt;

  ioi->id = ioi->opt->diskids++;
  ioi->status = RECSTATUS_OK;

  ioi->curfilename = (char*)malloc(sizeof(char)*FILENAME_MAX);
  //TODO: Set offset accordingly if file already exists. Not sure if
  //needed, since data consistency would take a hit anyway
  ioi->offset = 0;
  ioi->bytes_exchanged = 0;

  /* Only after everything ok add to the diskbranches */
  D("Adding writer %d to diskbranch",,ioi->id);
  struct listed_entity *le = (struct listed_entity*)malloc(sizeof(struct listed_entity));
  memset(le, 0, sizeof(struct listed_entity));
  le->entity = (void*)re;
  le->child = NULL;
  le->father = NULL;
  le->acquire = common_open_new_file;
  le->release = common_finish_file;
  le->check = check_if_suitable;
  le->identify = common_identify;
  le->infostring = common_infostring;
  le->close = common_close_and_free;
  re->self= le;

  err = pthread_mutex_init(&le->waitlock, NULL);
  CHECK_ERR("Waitlock init");
  err = pthread_cond_init(&le->waitsig, NULL);
  CHECK_ERR("Waitsig init");


  add_to_entlist(opt->diskbranch, le);
  D("Writer added to diskbranch");
  return 0;
}
INDEX_FILE_TYPE * common_pindex(struct recording_entity *re){
  struct common_io_info * ioi = re->opt;
  return ioi->indices;
  //return ((struct common_io_info)re->opt)->indices;
}
unsigned long common_nofpacks(struct recording_entity *re){
  struct common_io_info * ioi = re->opt;
  return ioi->indexfile_count;
  //return ((struct common_io_info*)re->opt)->indexfile_count;
}
void get_io_stats(void * opt, void * st){
  struct common_io_info *ioi = (struct common_io_info*)opt;
  struct stats * stats = (struct stats*)st;
  stats->total_written += ioi->bytes_exchanged;
}
int common_close(struct recording_entity * re, void * stats){
  D("Closing writer");
  //int err=0;
  struct common_io_info * ioi = (struct common_io_info*)re->opt;

  //struct stats* stat = (struct stats*)stats;
  //stat->total_written += ioi->bytes_exchanged;
  if(stats != NULL)
    get_io_stats(re->opt, stats);
  /*
     char * indexfile = malloc(sizeof(char)*FILENAME_MAX);
     sprintf(indexfile, "%s%s", ioi->filename, ".index");
     int statfd = open(ioi->filename, ioi->f_flags, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);

     close(statfd);
     */

  //Shrink to size we received if we're writing
  //if(ioi->curfilename != NULL)
  free(ioi->curfilename);
  free(ioi);
#if(DEBUG_OUTPUT)
  fprintf(stdout, "COMMON_WRT: Writer closed\n");
#endif
  return 0;
}
const char * common_wrt_get_filename(struct recording_entity *re){
  return ((struct common_io_info *)re->opt)->curfilename;
}
int common_getfd(struct recording_entity *re){
  return ((struct common_io_info*)re->opt)->fd;
}
off_t common_getfilesize(void *re)
{
  return ((struct common_io_info*)((struct recording_entity*)re)->opt)->filesize;
}
void common_setshmid(void *recco, int id, void * bufstart)
{
  struct recording_entity * re = (struct recording_entity*)recco;
  struct common_io_info * ioi = re->opt;
  ioi->shmid = id;
  ioi->bufstart = bufstart;
}
int common_check_files(struct recording_entity *re, void* opt_ss){
  int err=0;
  int temp=0;
  int retval = 0;
  int i,n_files;
  //int len;
  //struct recording_entity **temprecer;
  struct common_io_info * ioi = re->opt;
  struct opt_s* opt = (struct opt_s*)opt_ss;

  if(opt->optbits & WRITE_TO_SINGLE_FILE)
  {
    char filename[FILENAME_MAX];
    struct stat statbuf;
    sprintf(filename, "%s%i/%s/%s", ROOTDIRS, ioi->id, opt->filename,  opt->filename); 
    D("Writing/reading to/from single file %s",, filename);
    err = stat(filename, &statbuf);
    CHECK_ERR("Stat main data file");

    FI_WRITELOCK(opt->fi);
    struct fileholder* fh = opt->fi->files;
    n_files = opt->fi->n_files;
    for(i=0;i<n_files;i++)
    {
      fh->status &= ~FH_MISSING;
      fh->status |= FH_ONDISK;
      fh->diskid = ioi->id;
      opt->cumul_found++;
      fh++;
    }
    FIUNLOCK(opt->fi);
    return 0;
  }

  char * dirname = (char*)malloc(sizeof(char)*FILENAME_MAX);
  CHECK_ERR_NONNULL(dirname, "Dirname malloc");
  regex_t regex;

  sprintf(dirname, "%s%i%s%s%s", ROOTDIRS, ioi->id, "/",opt->filename, "/"); 
  D("Checking for files and updating fileholders on %s",, dirname);


  /* GRRR can't get [:digit:]{8} to work so I'll just do it manually */
  char* regstring = (char*)malloc(sizeof(char)*FILENAME_MAX);
  CHECK_ERR_NONNULL(regstring, "Malloc regexp string");
  sprintf(regstring, "^%s.[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]", opt->filename);
  //err = regcomp(&regex, "^[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]", 0);
  err = regcomp(&regex, regstring, 0);
  CHECK_ERR("Regcomp");

  /* print all the files and directories within directory */
  struct dirent **namelist;
  //int j;
  n_files = scandir(dirname, &namelist, NULL, NULL);
  if(n_files <0){
    // could not open directory 
    perror ("Check files");
    //FIUNLOCK(opt->fi);
    retval= EXIT_FAILURE;
  }
  else{
    FI_WRITELOCK(opt->fi);
    struct fileholder* fh = opt->fi->files;
    for(i=0;i<n_files;i++){
      err = regexec(&regex, namelist[i]->d_name, 0,NULL,0);
      /* If we match a data file */
      if( !err ){
	D("Regexp matched %s",, namelist[i]->d_name);
	/* Grab the INDEXING_LENGTH last chars from ent->d_name, which is the	*/
	/* The files index							*/
	char * start_of_index= namelist[i]->d_name+(strlen(namelist[i]->d_name))-INDEXING_LENGTH;
	//Hehe why don't I just use start_of_index..
	//memcpy(the_index,start_of_index,INDEXING_LENGTH);
	//temp = atoi(ent->d_name);
	//temp = atoi(the_index);
	temp = atoi(start_of_index);
	if((unsigned long)temp >= opt->fi->n_files)
	  E("Extra files found in dir named! Temp read %i, the_index: %s",, temp, start_of_index);
	else
	{
	  D("Identified %s as %d",,start_of_index,  temp);
	  fh = &(opt->fi->files[temp]);
	  fh->status &= ~FH_MISSING;
	  fh->status |= FH_ONDISK;
	  fh->diskid = ioi->id;
	  opt->cumul_found++;
	}
	}
	else if( err == REG_NOMATCH ){
	  D("Regexp didn't match %s",, namelist[i]->d_name);
	}
	else{
	  char msgbuf[100];
	  regerror(err, &regex, msgbuf, sizeof(msgbuf));
	  E("Regex match failed: %s",, msgbuf);
	  //exit(1);
	}
	free(namelist[i]);
      }
      free(namelist);
      FIUNLOCK(opt->fi);
    }
    //closedir (dir);
    D("Finished reading files in dir");
    /*
       } else {
       }
       */
    //free(ent);
    free(regstring);
    free(dirname);
    regfree(&regex);
    return retval;
}
void common_init_common_functions(struct opt_s * opt, struct recording_entity *re){
  (void)opt;
  re->init = common_w_init;
  re->close = common_close;
  re->get_n_packets = common_nofpacks;
  re->get_packet_index = common_pindex;
  re->get_stats = get_io_stats;

  re->writecfg = common_writecfg;
  re->readcfg = common_readcfg;
  re->check_files = common_check_files;
  re->handle_error = handle_error;
  re->getid = common_getid;
  re->get_filesize = common_getfilesize;
  re->setshmid = common_setshmid;

  re->get_filename = common_wrt_get_filename;
  re->getfd = common_getfd;
}
#if(HAVE_HUGEPAGES)
/*
 * Find hugetlbfs easily (usually /mnt/huge)
 * stolen from: http://lwn.net/Articles/375096/
 */
char *find_hugetlbfs(char *fsmount, int len)
{
  char format[256];
  char fstype[256];
  char *ret = NULL;
  FILE *fd;

  snprintf(format, 255, "%%*s %%%ds %%255s %%*s %%*d %%*d", len);

  fd = fopen("/proc/mounts", "r");
  if (!fd) {
    perror("fopen");
    return NULL;
  }

  while (fscanf(fd, format, fsmount, fstype) == 2) {
    if (!strcmp(fstype, "hugetlbfs")) {
      ret = fsmount;
      break;
    }
  }

  fclose(fd);
  return ret;
}
#endif

