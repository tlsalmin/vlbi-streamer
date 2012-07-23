#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#define O(...) fprintf(stdout, "File: %s: ", argv[1]);fprintf(stdout, __VA_ARGS__)

int main(int argc, char** argv){
  int fd,i;
  long count = 0;
  long fsize;
  long spacing;
  int scrambles;
  long read_count, read_count_temp;
  struct stat st;
  if(argc < 4){
    O("Usage: %s <filename> <byte spacing> <number of scrambles>\n", argv[0]);
    exit(-1);
  }
  
  if(stat(argv[1], &st) != 0){
    O("error in stat\n");
    exit(-1);
  }


  spacing = atol(argv[2]);
  scrambles = atoi(argv[3]);

  fd = open(argv[1], O_RDWR);
  if(fd == -1){
    O("Error opening file %s\n", argv[1]);
    exit(-1);
  }

  fsize = st.st_size;
  long scr1,scr2;
  count = fsize/spacing;
  O("count: %ld\n", count);

  /* Interchange two random seqnums scramble times	*/
  for(i=0;i < scrambles;i++){
    scr1 = random() % count;
    scr2 = random() % count;
    while(scr1 == scr2)
      scr2 = random() % count;


    lseek(fd, scr1*spacing, SEEK_SET);
    if(read(fd, &read_count,8) < 0){
      O("Read error\n");
      perror("read");
      exit(-1);
    }
    lseek(fd, scr2*spacing, SEEK_SET);
    if(read(fd, &read_count_temp,8) < 0){
      O("Read error\n");
      perror("read");
      exit(-1);
    }
    lseek(fd, scr2*spacing, SEEK_SET);
    if(write(fd, &read_count,8) < 0){
      O("Write error\n");
      perror("write");
      exit(-1);
    }
    lseek(fd, scr1*spacing, SEEK_SET);
    if(write(fd, &read_count_temp,8) < 0){
      O("Write error\n");
      perror("write");
      exit(-1);
    }
    if( i%4 == 0)
      fprintf(stdout, ".");
  }

  if(close(fd) != 0){
    O("Error on close\n");
    exit(-1);
  }

  exit(0);
}
