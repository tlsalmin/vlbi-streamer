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
#include "common_wrt.h"
/* There is a persistant bug where the writes fail with */
/* error EINVAL when the streamer has stopped IF the	*/
/* file was opened with IO_DIRECT. I've checked all the */
/* functions before it, disabled them etc. but can't 	*/
/* find the reason for this bug. This will disable the	*/
/* IO_DIRECT flag for the last write			*/
#define ERR_IN_INIT free(ioi);return -1


/* These should be moved somewhere general, since they should be used by all anyway */
/* writers anyway */
int common_open_new_file(void * recco, void *opti,unsigned long seq, unsigned long sbuf_still_running){
  (void)sbuf_still_running;
  int err;
  struct recording_entity * re = (struct recording_entity*)recco;
  struct common_io_info * ioi = (struct common_io_info*)re->opt;
  D("Acquiring new recorder and changing opt");
  ioi->opt = (struct opt_s*)opti;
  int tempflags = ioi->f_flags;
  ioi->file_seqnum = seq;

  ioi->curfilename = (char*)malloc(sizeof(char)*FILENAME_MAX);
  sprintf(ioi->curfilename, "%s%i%s%s%s%s.%08ld", ROOTDIRS, ioi->id, "/",ioi->opt->filename, "/",ioi->opt->filename,seq); 

  D("Opening file %s",,ioi->curfilename);
  err = common_open_file(&(ioi->fd),tempflags, ioi->curfilename, 0);
  if(err!=0){
    /* Situation where the directory doesn't yet exist but we're 	*/
    /* writing so this shouldn't be a failure 				*/
    if((err & (ENOTDIR|ENOENT)) && !(ioi->opt->optbits & READMODE)){
      D("Directory doesn't exist. Creating it");
      init_directory(re);
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
  return 0;
}
int common_finish_file(void *recco){
  struct recording_entity * re = (struct recording_entity*)recco;
  struct common_io_info * ioi = (struct common_io_info*)re->opt;
  int ret = close(ioi->fd);
  if(ret != 0)
    E("Error in closing file!");
  else
    D("File closed");
  free(ioi->curfilename);
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
#if(DEBUG_OUTPUT)
	fprintf(stdout, "COMMON_WRT: File doesn't exist. Creating it\n");
#endif
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
    fprintf(stderr,"Error: %s on %s\n",strerror(errno), filename);
    return *fd;
  }
#if(DEBUG_OUTPUT)
  fprintf(stdout, "COMMON_WRT: File opened\n");
#endif
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
#if(DEBUG_OUTPUT)
    fprintf(stdout, "COMMON_WRT: File preallocated\n");
#endif
  }
#if(DEBUG_OUTPUT)
  else
    fprintf(stdout, "COMMON_WRT: Not fallocating\n");
#endif
  return err;
}
int common_writecfg(struct recording_entity *re, void *opti){
  int err;
  struct common_io_info *ioi  = re->opt;
  struct opt_s * opt = opti;
  char * cfgname = (char*) malloc(sizeof(char)*FILENAME_MAX);

  sprintf(cfgname, "%s%i%s%s%s%s%s", ROOTDIRS, ioi->id, "/",opt->filename, "/",opt->filename, ".cfg"); 
  err = write_cfg(&(opt->cfg), cfgname);
  free(cfgname);

  return err;
}
int common_readcfg(struct recording_entity *re, void *opti){
  int err;
  struct common_io_info *ioi  = re->opt;
  struct opt_s * opt = opti;
  char * cfgname = (char*) malloc(sizeof(char)*FILENAME_MAX);

  sprintf(cfgname, "%s%i%s%s%s%s%s", ROOTDIRS, ioi->id, "/",opt->filename, "/",opt->filename, ".cfg"); 
  err = read_cfg(&(opt->cfg), cfgname);
  free(cfgname);

  return err;
}
int common_write_index_data(const char * filename_orig, long unsigned elem_size, void *data, long unsigned count){
  //struct io_info * ioi = (struct io_info*)re->opt;
  int err = 0;
  char * filename = (char*)malloc(sizeof(char)*FILENAME_MAX);
  int fd;

#if(DEBUG_OUTPUT)
  fprintf(stdout, "COMMON_WRT: Writing index file\n");
#endif
  sprintf(filename, "%s%s", filename_orig, ".index");
  int f_flags = O_WRONLY;//|O_DIRECT|O_NOATIME|O_NONBLOCK;

  err = common_open_file(&fd, f_flags, filename, 0);
  if(err != 0){
    fprintf(stdout, "COMMON_WRT: File open failed when trying to write index data on %s\n", filename);
    return err;
  }

  FILE * file = fdopen(fd, "w");
  //Write the elem size to the first index
  //err = write(fd, (void*)&(elem_size), sizeof(INDEX_FILE_TYPE));
  err = fwrite((void*)&(elem_size), sizeof(elem_size), 1, file);
  if(err != 1){
    if(err == -1)
      perror("COMMON_WRT: Index file size write");
    else
      fprintf(stderr, "COMMON_WRT: Index file size write didn't write as much as requested!. Wrote only %d\n", err);
    return err;
  }
  else{
#if(DEBUG_OUTPUT)
    fprintf(stdout, "COMMON_WRT: Wrote %lu as elem size\n",elem_size);
    INDEX_FILE_TYPE *debughelp = data;
    fprintf(stdout, "COMMON_WRT: First indices: %lu %lu %lu %lu\n", *debughelp, *(debughelp+1),*(debughelp+2),*(debughelp+3));
#endif
    err =0;
  }
  //Write the data
  //err = write(fd, data, count*sizeof(INDEX_FILE_TYPE));
  err = fwrite(data, sizeof(INDEX_FILE_TYPE), count, file);
  if((unsigned long)err != count){
    if(err == -1)
      perror("COMMON_WRT: Writing indices");
    else
      fprintf(stderr, "COMMON_WRT: indice writedidn't write as much as requested!. Wrote only %d\n", err);
    return err;
  }
  else{
#if(DEBUG_OUTPUT)
    fprintf(stdout, "COMMON_WRT: Wrote %lu as elem size\n",elem_size);
    INDEX_FILE_TYPE *debughelp = data;
    fprintf(stdout, "COMMON_WRT: First indices: %lu %lu %lu %lu\n", *debughelp, *(debughelp+1),*(debughelp+2),*(debughelp+3));
#endif
    err =0;
  }
  fclose(file);
  free(filename);
#if(DEBUG_OUTPUT)
  fprintf(stdout, "COMMON_WRT: Index file written\n");
#endif

  return err;
}
long common_fake_recer_write(struct recording_entity * re, void* s, size_t count){
  (void)re;
  (void)s;
  return count;
}
int common_close_dummy(struct recording_entity *re, void *st){
  (void)st;
  free((struct common_io_info*)re->opt);
  return 0;
}
int common_init_dummy(struct opt_s * opt, struct recording_entity *re){
  re->opt = (void*)malloc(sizeof(struct common_io_info));
  re->write = common_fake_recer_write;
  re->close = common_close_dummy;
  D("Adding writer to diskbranch");
  struct listed_entity *le = (struct listed_entity*)malloc(sizeof(struct listed_entity));
  le->entity = (void*)re;
  le->child = NULL;
  le->father = NULL;
  //le->acquire = common_open_new_file;
  //le->release = common_finish_file;
  re->self= le;
  add_to_entlist(opt->diskbranch, le);
  D("Writer added to diskbranch");

  //be->recer->write_index_data = NULL;
  //return rbuf_init_buf_entity(opt,be);
  return 0;
}
int common_check_id(void *recco, int id){
  struct recording_entity *re = (struct recording_entity *)recco;
  struct common_io_info* ioi = re->opt;
  //D("Asked for %d, we are %d",, ioi->id,id);
  if(ioi->id == id)
    return 1;
  else
    return 0;
  //return (((struct common_io_info*)((struct recording_entity*)recco)->opt)->id == id);
}
int common_close_and_free(void* recco){
  if(recco != NULL){
    struct recording_entity * re = (struct recording_entity *)recco;
    //re->close(re,NULL);
    free(re);
  }
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

    if(stat(dirname,&sb) != 0){
      perror("Error Opening dir");
      ERR_IN_INIT;
    }
    else if (!S_ISDIR(sb.st_mode)){
      E("%s Not a directory. Not initializing this reader",,dirname);
      ERR_IN_INIT;
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
	ERR_IN_INIT;
      }
    }
  }
  free(dirname);
  return 0;
}
int common_w_init(struct opt_s* opt, struct recording_entity *re){
  //void * errpoint;
  re->opt = (void*)malloc(sizeof(struct common_io_info));
  re->get_stats = get_io_stats;
  struct common_io_info * ioi = (struct common_io_info *) re->opt;
  //loff_t prealloc_bytes;
  //struct stat statinfo;
  int err =0;
  //ioi->opt->optbits = opt->optbits;
  ioi->opt = opt;

  ioi->id = ioi->opt->diskids++;
  err = init_directory(re);
  CHECK_ERR("Init directory");

  //ioi->latest_write_num = 0;
  if(ioi->opt->optbits & READMODE){
#if(DEBUG_OUTPUT)
    fprintf(stdout, "COMMON_WRT: Initializing read point\n");
    fprintf(stdout, "COMMON_WRT: Getting read flags\n");
#endif
    ioi->f_flags = re->get_r_flags();
    //prealloc_bytes = 0;
  }
  else{
#if(DEBUG_OUTPUT)
    fprintf(stdout, "COMMON_WRT: Initializing write point\n");
    fprintf(stdout, "COMMON_WRT: Getting write flags and calculating falloc\n");
#endif
    //ioi->f_flags = O_WRONLY|O_DIRECT|O_NOATIME|O_NONBLOCK;
    ioi->f_flags = re->get_w_flags();

    /* Why did i do this twice? */
    //RATE = 10 Gb => RATE = 10*1024*1024*1024/8 bytes/s. Handled on n_threads
    //for s seconds.
    //prealloc_bytes = ((unsigned long)opt->rate*opt->time)/(opt->n_threads*8);
    //Split kb/gb stuff to avoid overflow warning
    //prealloc_bytes = prealloc_bytes*1024*1024;
    //set flag FALLOC_FL_KEEP_SIZE to precheck drive for errors

    //prealloc_bytes = opt->max_num_packets* opt->packet_size;
    //prealloc_bytes=0;

  }
  //fprintf(stdout, "wut\n");
  //ioi->filename = opt->filenames[opt->taken_rpoints++];


  //TODO: Set offset accordingly if file already exists. Not sure if
  //needed, since data consistency would take a hit anyway
  ioi->offset = 0;
  ioi->bytes_exchanged = 0;

  /* Only after everything ok add to the diskbranches */
  D("Adding writer to diskbranch");
  struct listed_entity *le = (struct listed_entity*)malloc(sizeof(struct listed_entity));
  le->entity = (void*)re;
  le->child = NULL;
  le->father = NULL;
  le->acquire = common_open_new_file;
  le->release = common_finish_file;
  le->check = common_check_id;
  le->close = common_close_and_free;
  re->self= le;
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
  if(!(ioi->opt->optbits & READMODE)){
    D("Truncating file");
    /* Enable when we're fallocating again */
    /*
       err = ftruncate(ioi->fd, ioi->bytes_exchanged);
       if(err!=0){
       perror("COMMON_WRT: ftruncate");
       return err;
       }
       */
  }
  else{
    //free(ioi->indices);
    /* No need to close indice-file since it was read into memory */
  }
  //remove_from_branch(ioi->opt->diskbranch, re->self, 0);
  //free(re->self);
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
int common_check_files(struct recording_entity *re, void* opti){
  int err;
  int temp;
  //struct recording_entity **temprecer;
  struct opt_s* opt = (struct opt_s*)opti;
  struct common_io_info * ioi = re->opt;
  char * dirname = (char*)malloc(sizeof(char)*FILENAME_MAX);
  regex_t regex;
  D("Checking for files and updating fileholders");

  sprintf(dirname, "%s%i%s%s%s", ROOTDIRS, ioi->id, "/",opt->filename, "/"); 
  /* GRRR can't get [:digit:]{8} to work so I'll just do it manually */
  char* regstring = (char*)malloc(sizeof(char)*FILENAME_MAX);
  sprintf(regstring, "^%s.[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]", opt->filename);
  //err = regcomp(&regex, "^[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]", 0);
  err = regcomp(&regex, regstring, 0);
  free(regstring);
  CHECK_ERR("Regcomp");


  DIR *dir;
  struct dirent *ent;
  dir = opendir(dirname);
  free(dirname);
  if (dir != NULL) {

    /* print all the files and directories within directory */
    while ((ent = readdir (dir)) != NULL) {
      err = regexec(&regex, ent->d_name, 0,NULL,0);
      /* If we match a data file */
      if( !err ){
	D("Regexp matched %s",, ent->d_name);
	char the_index[INDEXING_LENGTH];
	/* Grab the INDEXING_LENGTH last chars from ent->d_name, which is the	*/
	/* The files index							*/
	char * start_of_index= ent->d_name+(strlen(ent->d_name))-INDEXING_LENGTH;
	memcpy(the_index,start_of_index,INDEXING_LENGTH);
	//temp = atoi(ent->d_name);
	temp = atoi(the_index);
	if((unsigned long)temp >= ioi->opt->cumul)
	  E("Extra files found in dir!");
	else
	{
	  /* Update pointer at correct spot */
	  //temprecer = opt->fileholders + temp*sizeof(*);
	  //*temprecer = re;
	  opt->fileholders[temp] = ioi->id;
	  ioi->opt->cumul_found++;
	}
      }
      else if( err == REG_NOMATCH ){
	D("Regexp didn't match %s",, ent->d_name);
      }
      else{
	char msgbuf[100];
	regerror(err, &regex, msgbuf, sizeof(msgbuf));
	E("Regex match failed: %s",, msgbuf);
	//exit(1);
      }
    }
    D("Finished reading files in dir");
    closedir (dir);
  } else {
    /* could not open directory */
    perror ("Check files");
    return EXIT_FAILURE;
  }

  regfree(&regex);
  return 0;
}
void common_init_common_functions(struct opt_s * opt, struct recording_entity *re){
  (void)opt;
  re->init = common_w_init;
  re->close = common_close;
  re->write_index_data = common_write_index_data;
  re->get_n_packets = common_nofpacks;
  re->get_packet_index = common_pindex;
  re->get_stats = get_io_stats;

  re->writecfg = common_writecfg;
  re->readcfg = common_readcfg;
  re->check_files = common_check_files;

  re->get_filename = common_wrt_get_filename;
  re->getfd = common_getfd;
}
#ifdef HAVE_HUGEPAGES
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

