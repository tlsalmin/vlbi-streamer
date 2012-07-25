#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/stat.h>
#define O(...) fprintf(stdout, "File: %s: ", argv[1]);fprintf(stdout, __VA_ARGS__)

int main(int argc, char** argv){
  int fd,i;
  long count = 0;
  long fsize;
  long spacing;
  long read_count;
  struct stat st;
  if(argc < 3){
    O("Usage: %s <filename> <byte spacing>\n", argv[0]);
    exit(-1);
  }

  
  if(stat(argv[1], &st) != 0){
    O("error in stat\n");
    exit(-1);
  }

  spacing = atol(argv[2]);

  fd = open(argv[1], O_RDONLY);
  if(fd == -1){
    O("Error opening file %s\n", argv[1]);
    exit(-1);
  }

  fsize = st.st_size;
  

  if(read(fd, &count,8) < 0){
    O("Read error!");
    exit(-1);
  }
  count = be64toh(count);
  O("first count is %ld\n", count);
  count++;

  for(i=1;i*spacing < fsize;i++){

    lseek(fd, i*spacing, SEEK_SET);

    if(read(fd, &read_count,8) < 0){
      O("Read error!\n");
      break;
    }
    read_count = be64toh(read_count);
    if(count != read_count){
      fprintf(stdout, "Discrepancy as count is %ld and read_count is %ld\n",count, read_count);
      //count = read_count;
    }
    count++;
  }
  fprintf(stdout, "Done!\n");

  if(close(fd) != 0){
    O("Error on close\n");
    exit(-1);
  }

  exit(0);
}
