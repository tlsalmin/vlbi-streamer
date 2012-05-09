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
#include "config.h"


#include "streamer.h"
#include "common_wrt.h"


/* These should be moved somewhere general, since they should be used by all anyway */
/* writers anyway */
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
	perror("COMMON_WRT: File not found, eventhought we wan't to read");
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
    else{
      fprintf(stderr,"Error: %s on %s\n",strerror(errno), filename);
      return err;
    }
  }

  //This will overwrite existing file.TODO: Check what is the desired default behaviour 
#if(DEBUG_OUTPUT)
  fprintf(stdout, "COMMON_WRT: Opening file %s\n", filename);
#endif
  *fd = open(filename, flags, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
  if(*fd < 0){
    fprintf(stderr,"Error: %s on %s\n",strerror(errno), filename);
    return errno;
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
int common_handle_indices(struct common_io_info *ioi){
  char * filename = (char*)malloc(sizeof(char)*FILENAME_MAX);
  int f_flags = O_RDONLY;//|O_DIRECT|O_NOATIME|O_NONBLOCK;
  int fd,err = 0;
  //unsigned long num_elems;
  //Duplicate stat here, since first done in aiow_read, but meh.
  struct stat statinfo;


  sprintf(filename, "%s%s", ioi->filename, ".index");

  err = common_open_file(&fd, f_flags, filename, 0);
  if(err != 0){
    fprintf(stderr, "COMMON_WRT: Failed to read index data on %s", filename);
    return err;
  }

  /*
   * elem_size = Size of the packets we received when receiving the stream
   * INDEX_FILE_TYPE = The size of our index-counter.(Eg 32bit integer or 64bit).
   */
  //Read the elem size from the first index
  err = read(fd, &(ioi->elem_size), sizeof(INDEX_FILE_TYPE));
  if(err<0){
    perror("COMMON_WRT: Index file size read");
    return err;
  }
#if(DEBUG_OUTPUT)
  fprintf(stdout, "COMMON_WRT: Elem size here %lu\n", ioi->elem_size);
#endif 

  //ioi->elem_size = err;

  err = fstat(fd, &statinfo);
  if(err<0){
    perror("FD stat from index");
    return err;
  }
  //NOTE: Reducing first element, which simply tells size of elements
  ioi->indexfile_count = (statinfo.st_size-sizeof(INDEX_FILE_TYPE))/sizeof(INDEX_FILE_TYPE);
#if(DEBUG_OUTPUT)
  fprintf(stdout, "COMMON_WRT: Total number of elements in index file: %lu\n", ioi->indexfile_count);
#endif
  ioi->indices = (INDEX_FILE_TYPE*) malloc(sizeof(INDEX_FILE_TYPE)*(ioi->indexfile_count));
  if(ioi->indices == NULL){
    fprintf(stderr, "COMMON_WRT: Indice malloc failed!\n");
    return -1;
  }


  //*count = 0;
  INDEX_FILE_TYPE* pointer = ioi->indices;
  while((err = read(fd, pointer, sizeof(INDEX_FILE_TYPE)))>0){
    pointer++;
    //count += 1;
  }
  if(err <0){
    fprintf(stderr, "COMMON_WRT: Error when reading indices %s\n", strerror(errno));
    return err;
  }
  err = 0;
  
  close(fd);
  free(filename);

  return err;
}
long common_fake_recer_write(struct recording_entity * re, void* s, size_t count){
  return count;
}
int common_close_dummy(struct recording_entity *re, void *st){
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
  re->self= le;
  add_to_entlist(opt->diskbranch, le);
  D("Writer added to diskbranch");

  //be->recer->write_index_data = NULL;
  //return rbuf_init_buf_entity(opt,be);
  return 0;
}
int common_w_init(struct opt_s* opt, struct recording_entity *re){
  //void * errpoint;
  re->opt = (void*)malloc(sizeof(struct common_io_info));
  re->get_stats = get_io_stats;
  struct common_io_info * ioi = (struct common_io_info *) re->opt;
  loff_t prealloc_bytes;
  //struct stat statinfo;
  int err =0;
  ioi->optbits = opt->optbits;
  ioi->opt = opt;


  //re->diskbranch = &(opt->diskbranch);
  D("Adding writer to diskbranch");
  struct listed_entity *le = (struct listed_entity*)malloc(sizeof(struct listed_entity));
  le->entity = (void*)re;
  le->child = NULL;
  le->father = NULL;
  re->self= le;
  add_to_entlist(opt->diskbranch, le);
  D("Writer added to diskbranch");


  //ioi->latest_write_num = 0;
  if(ioi->optbits & READMODE){
#if(DEBUG_OUTPUT)
    fprintf(stdout, "COMMON_WRT: Initializing read point\n");
    fprintf(stdout, "COMMON_WRT: Getting read flags\n");
#endif
    ioi->f_flags = re->get_r_flags();
    prealloc_bytes = 0;
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

    //prealloc_bytes = opt->max_num_packets* opt->buf_elem_size;
    prealloc_bytes=0;

  }
  //fprintf(stdout, "wut\n");
  ioi->filename = opt->filenames[opt->taken_rpoints++];
  err = common_open_file(&(ioi->fd),ioi->f_flags, ioi->filename, prealloc_bytes);
  if(err!=0){
    fprintf(stderr, "COMMON_WRT: Init: Error in file open: %s\n", ioi->filename);
    return err;
  }
  //TODO: Set offset accordingly if file already exists. Not sure if
  //needed, since data consistency would take a hit anyway
  ioi->offset = 0;
  ioi->bytes_exchanged = 0;
  if(ioi->optbits & READMODE){
    err = common_handle_indices(ioi);
    if(err != 0){
      fprintf(stderr, "COMMON_WRT: Reading indices returned %d", err);
      return -1;
    }
    else{
      opt->buf_elem_size = ioi->elem_size;
      /* If we haven't calculated optsize yet */
      if(opt->buf_num_elems == 0)
	calculate_buffer_sizes(opt);
#if(DEBUG_OUTPUT)
      fprintf(stdout, "Element size is %lu\n", opt->buf_elem_size);
#endif
    }
  }
  else{
    ioi->elem_size = opt->buf_elem_size;
  }
  err = 0;
  return err;
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
  int err=0;
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
  if(!(ioi->optbits & READMODE)){
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
  close(ioi->fd);
  D("File closed");

  /*
  remove_from_branch(ioi->opt->diskbranch, re->self);
  D("Removed from branch");
  */

  //ioi->ctx = NULL;
  free(ioi->filename);

  free(ioi);
#if(DEBUG_OUTPUT)
  fprintf(stdout, "COMMON_WRT: Writer closed\n");
#endif
  return 0;
}
const char * common_wrt_get_filename(struct recording_entity *re){
  return ((struct common_io_info *)re->opt)->filename;
}
int common_getfd(struct recording_entity *re){
  return ((struct common_io_info*)re->opt)->fd;
}
void common_init_common_functions(struct opt_s * opt, struct recording_entity *re){
  re->init = common_w_init;
  re->close = common_close;
  re->write_index_data = common_write_index_data;
  re->get_n_packets = common_nofpacks;
  re->get_packet_index = common_pindex;
  re->get_stats = get_io_stats;

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

