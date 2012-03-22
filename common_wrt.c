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


#include "streamer.h"
#include "common_wrt.h"


/* These should be moved somewhere general, since they should be used by all anyway */
/* writers anyway */
int common_open_file(int *fd, int flags, char * filename, loff_t fallosize){
  struct stat statinfo;
  int err =0;

  //ioi->latest_write_num = 0;
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Initializing write point\n");
#endif
  //Check if file exists
  //ioi->f_flags = O_WRONLY|O_DIRECT|O_NOATIME|O_NONBLOCK;
  //ioi->filename = opt->filenames[opt->taken_rpoints++];
  err = stat(filename, &statinfo);
  if (err < 0) {
    if (errno == ENOENT){
      //We're reading the file
      if(flags & O_RDONLY){
	perror("AIOW: File not found, eventhought we're in send-mode");
	return -1;
      }
      else{
#ifdef DEBUG_OUTPUT
	fprintf(stdout, "File doesn't exist. Creating it\n");
#endif
	flags |= O_CREAT;
	err = 0;
      }
    }
    else{
      fprintf(stderr,"Error: %s on %s\n",strerror(errno), filename);
      return -1;
    }
  }

  //This will overwrite existing file.TODO: Check what is the desired default behaviour 
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "Opening file %s\n", filename);
#endif
  *fd = open(filename, flags, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
  if(*fd == -1){
    fprintf(stderr,"Error: %s on %s\n",strerror(errno), filename);
    return -1;
  }
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: File opened\n");
#endif
  if(fallosize > 0){
    err = fallocate(*fd, 0,0, fallosize);
    if(err == -1){
      fprintf(stderr, "Fallocate failed on %s", filename);
      return err;
    }
#ifdef DEBUG_OUTPUT
    fprintf(stdout, "AIOW: File preallocated\n");
#endif
  }
  return err;
}
int common_write_index_data(const char * filename_orig, int elem_size, void *data, int count){
  //struct io_info * ioi = (struct io_info*)re->opt;
  int err = 0;
  char * filename = (char*)malloc(sizeof(char)*FILENAME_MAX);
  int fd;

#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Writing index file\n");
#endif
  sprintf(filename, "%s%s", filename_orig, ".index");
  int f_flags = O_WRONLY;//|O_DIRECT|O_NOATIME|O_NONBLOCK;

  common_open_file(&fd, f_flags, filename, 0);

  //Write the elem size to the first index
  err = write(fd, (void*)&(elem_size), sizeof(INDEX_FILE_TYPE));
  if(err<0)
    perror("AIOW: Index file size write");
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "Wrote %d as elem size",elem_size);
#endif

  //Write the data
  err = write(fd, data, count*sizeof(INDEX_FILE_TYPE));
  if(err<0){
    perror("AIOW: Index file write");
    fprintf(stderr, "Filename was %s\n", filename);
  }

  close(fd);
  free(filename);
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "AIOW: Index file written\n");
#endif

  return err;
}
int common_handle_indices(const char *filename_orig, INDEX_FILE_TYPE * elem_size, void * pindex, INDEX_FILE_TYPE *count){
  char * filename = (char*)malloc(sizeof(char)*FILENAME_MAX);
  int f_flags = O_RDONLY;//|O_DIRECT|O_NOATIME|O_NONBLOCK;
  int fd,err;
  int num_elems;
  //Duplicate stat here, since first done in aiow_read, but meh.
  struct stat statinfo;


  sprintf(filename, "%s%s", filename_orig, ".index");

  common_open_file(&fd, f_flags, filename, 0);

  /*
   * elem_size = Size of the packets we received when receiving the stream
   * INDEX_FILE_TYPE = The size of our index-counter.(Eg 32bit integer or 64bit).
   */
  //Read the elem size from the first index
  err = read(fd, elem_size, sizeof(INDEX_FILE_TYPE));
  if(err<0){
    perror("AIOW: Index file size read");
    return err;
  }
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "Elem size here %lu\n", *elem_size);
#endif 

  //ioi->elem_size = err;

  err = fstat(fd, &statinfo);
  if(err<0){
    perror("FD stat from index");
    return err;
  }
  //NOTE: Reducing first element, which simply tells size of elements
  num_elems = (statinfo.st_size-sizeof(INDEX_FILE_TYPE))/sizeof(INDEX_FILE_TYPE);
  pindex = (INDEX_FILE_TYPE*) malloc(sizeof(INDEX_FILE_TYPE)*(num_elems));

  *count = 0;
  INDEX_FILE_TYPE* pointer = pindex;
  while((err = read(fd, pointer, sizeof(INDEX_FILE_TYPE)))>0){
    pointer++;
    count += 1;
  }
  
  close(fd);
  free(filename);

  return err;
}
