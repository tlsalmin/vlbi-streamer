#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/mman.h>
#include <errno.h>

#include "streamer.h"
#include "confighelper.h"
#include "config.h"

//int main(int argc, char **argv) {
int disk2file(int argc, char ** argv){
  int err =0;
  int pipefd[2];
  int result;

  struct opt_s * opt = (struct opt_s*)malloc(sizeof(struct opt_s));

  //struct common_control_element *cce = (struct common_control_element*)malloc(sizeof(struct common_control_element));

  /*
  err = getopts(argc, argv, cce);
  if(err != 0){
    O("Error in getopts");
    exit(-1);
  }
  */

  err = clear_and_default(opt, 1);
  if(err != 0)
  {
    E("Error in opt clear");
    return -1;
  }
  err = parse_options(argc, argv,opt);
  if(err != 0){
    E("Error in opt parse");
    return -1;
  }

  FILE *in_file;
  FILE *out_file;

  result = pipe(pipefd);

  in_file = fopen(argv[1], "rb");
  out_file = fopen(argv[2], "wb");

  result = splice(fileno(in_file), 0, pipefd[1], NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
  printf("%d\n", result);

  result = splice(pipefd[0], NULL, fileno(out_file), 0, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
  printf("%d\n", result);

  if (result == -1)
    printf("%d - %s\n", errno, strerror(errno));

  close(pipefd[0]);
  close(pipefd[1]);
  fclose(in_file);
  fclose(out_file);


  free(opt);
  return 0;
}
