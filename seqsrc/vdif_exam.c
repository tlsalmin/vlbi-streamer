#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/stat.h>
#define O(...) fprintf(stdout, "File: %s: ", argv[1]);fprintf(stdout, __VA_ARGS__)

int main(int argc, char ** argv){
  int fd,i;
  int count = 0;
  int framenum;
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

  for(i=1;i*spacing < fsize;i++){
    lseek(fd, i*spacing, SEEK_SET);
    if(read(fd, &count,4) < 0){
      O("Read error!");
      exit(-1);
    }
    int countend = count & 0xfffffff3;
    int countstart = count & 0x3fffffff;
    //fprintf(stdout, "First count %x, switch: %x andend %x andstart %x switchend: %x, switchstart %x\n", count, be32toh(count), countend, countstart, be32toh(countend), be32toh(countstart));
    fprintf(stdout, "First count %d, switch: %d andend %d andstart %d switchend: %d, switchstart %d\n", count, be32toh(count), countend, countstart, be32toh(countend), be32toh(countstart));

    if(read(fd, &framenum,4) < 0){
      O("Read error!");
      exit(-1);
    }
    framenum = framenum & 0x00ffffff;
    fprintf(stdout, "dat framenum: %d\n", framenum);

    //(void)i;
  }

    (void)read_count;
    return 0;
  }
