#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <endian.h>

int main(int argc, char** argv){
  int fd;
  long count = 0;
  long tohcount;
  long fsize;
  long spacing;
  struct stat st;
  if(argc < 3){
    fprintf(stderr, "Usage: %s <filename> <byte spacing>\n", argv[0]);
    exit(-1);
  }

  
  if(stat(argv[1], &st) != 0){
    fprintf(stderr, "error in stat\n"); 
    exit(-1);
  }

  spacing = atol(argv[2]);

  fd = open(argv[1], O_WRONLY);
  if(fd == -1){
    fprintf(stderr, "Error opening file %s\n", argv[1]);
    exit(-1);
  }

  fsize = st.st_size;
  
  //fprintf(stdout, "filename %s, spacing: %ld, filesize %ld", argv[1], spacing, fsize);

  while(count*spacing < fsize){
    tohcount = htobe64(count);
    if(write(fd, &tohcount, 8) < 0){
      fprintf(stderr, "Write error!");
      break;
    }
    count++;
    lseek(fd, count*spacing, SEEK_SET);
  }

  if(close(fd) != 0){
    fprintf(stderr, "Error on close\n");
    exit(-1);
  }

  exit(0);
}
