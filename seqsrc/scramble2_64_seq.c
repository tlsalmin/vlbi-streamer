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
  int fd1,fd2,i;
  long count = 0;
  long fsize;
  long spacing;
  int scrambles;
  void* packet1,* packet2;
  struct stat st;
  if(argc < 4){
    O("Usage: %s <filename> <filename2> <byte spacing> <number of scrambles>\n", argv[0]);
    exit(-1);
  }
  
  if(stat(argv[1], &st) != 0){
    O("error in stat for %s\n", argv[1]);
    exit(-1);
  }
  if(stat(argv[2], &st) != 0){
    O("error in stat for %s\n", argv[2]);
    exit(-1);
  }


  spacing = atol(argv[3]);
  scrambles = atoi(argv[4]);

  fd1= open(argv[1], O_RDWR);
  if(fd1== -1){
    O("Error opening file %s\n", argv[1]);
    exit(-1);
  }

  fd2= open(argv[2], O_RDWR);
  if(fd2== -1){
    O("Error opening file %s\n", argv[2]);
    exit(-1);
  }

  fsize = st.st_size;
  long scr1,scr2;
  count = fsize/spacing;
  O("count: %ld\n", count);
  packet1 = malloc(spacing);
  packet2 = malloc(spacing);

  /* Interchange two random seqnums scramble times	*/
  for(i=0;i < scrambles;i++){
    scr1 = random() % count;
    scr2 = random() % count;
    while(scr1 == scr2)
      scr2 = random() % count;


    lseek(fd1, scr1*spacing, SEEK_SET);
    if(read(fd1, packet1,spacing) < 0){
      O("Read error\n");
      perror("read");
      exit(-1);
    }
    lseek(fd2, scr2*spacing, SEEK_SET);
    if(read(fd2, packet2,spacing) < 0){
      O("Read error\n");
      perror("read");
      exit(-1);
    }
    lseek(fd2, scr2*spacing, SEEK_SET);
    if(write(fd2, packet1,spacing) < 0){
      O("Write error\n");
      perror("write");
      exit(-1);
    }
    lseek(fd1, scr1*spacing, SEEK_SET);
    if(write(fd1, packet2,spacing) < 0){
      O("Write error\n");
      perror("write");
      exit(-1);
    }
    if( i%4 == 0)
      fprintf(stdout, ".");
  }
  fprintf(stdout, "Done!\n");

  free(packet1);
  free(packet2);
  if(close(fd1) != 0){
    O("Error on close\n");
    exit(-1);
  }
  if(close(fd2) != 0){
    O("Error on close\n");
    exit(-1);
  }

  exit(0);
}

