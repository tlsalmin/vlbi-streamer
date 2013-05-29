#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <ctype.h>
#define O(...) fprintf(stdout, "File: %s: ", argv[1]);fprintf(stdout, __VA_ARGS__)
void usage(char**argv){
    O("Usage: %s <filename> <byte spacing>\n", argv[0]);

  exit(-1);
}

int main(int argc, char** argv){
  int fd,i;
  long count = 0;
  long fsize;
  long spacing=0;
  long read_count;
  int readoffset=0;
  int wordsize=8;
  struct stat st;
  char* filename = NULL;
  int index;
  int bflag=0;
  char c;
  while ((c = getopt (argc, argv, "w:bo:")) != -1)
    switch (c)
    {
      case 'w':
	wordsize = atoi(optarg);
	break;
      case 'o':
	readoffset= atoi(optarg);
	break;
      case 'b':
	bflag = 1;
	break;
      case '?':
	if (optopt == 'w' || optopt == 'o')
	  fprintf (stderr, "Option -%c requires an argument.\n", optopt);
	else if (isprint (optopt)){
	  fprintf (stderr, "Unknown option `-%c'.\n", optopt);
	  usage(argv);
	}
	else{
	  fprintf (stderr,
	      "Unknown option character `\\x%x'.\n",
	      optopt);
	  usage(argv);
	}
      default:{
	abort ();
	      }
    }

  printf ("wordsize = %d, bflag = %d\n",
      wordsize, bflag);

  int j =0;
  for (index = optind; index < argc; index++){
    printf("Non-option argument %s\n", argv[index]);
    if(j == 0){
      filename = argv[index];
      O("Filename is %s\n", filename);
      j++;
    }
    else if(j == 1)
    {
      spacing = atol(argv[index]);
      O("Spacing is %ld\n", spacing);
      j++;
    }
  }
  if(filename == NULL || spacing == 0){
    fprintf(stderr,"No filename or no spacing\n");
    return -1;
  }
  //return 0;
  /*
  if(argc < 3){
    O("Usage: %s <filename> <byte spacing>\n", argv[0]);
    exit(-1);
  }
  */

  
  if(stat(filename, &st) != 0){
    O("error in stat\n");
    exit(-1);
  }

  //spacing = atol(argv[2]);

  fd = open(filename, O_RDONLY);
  if(fd == -1){
    O("Error opening file %s\n", filename);
    exit(-1);
  }


  fsize = st.st_size;
  

  lseek(fd,readoffset,SEEK_SET);
  if(read(fd, &count,wordsize) < 0){
    O("Read error!");
    exit(-1);
  }
  if(bflag == 0)
    count = be64toh(count);
  O("first count is %ld\n", count);
  count++;

  for(i=1;i*spacing < fsize;i++){

    lseek(fd, i*spacing+readoffset, SEEK_SET);

    if(read(fd, &read_count,wordsize) < 0){
      O("Read error!\n");
      break;
    }
    if(bflag==0)
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
