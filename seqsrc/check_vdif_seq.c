#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/stat.h>
#define O(...) fprintf(stdout, "File: %s: ", argv[1]);fprintf(stdout, __VA_ARGS__)
#define VDIFSIZE 8224
#define BOLPRINT(x) (x)?"true":"false"
#define DATATYPEPRINT(x) (x)?"Complex":"Real"
#define GRAB_4_BYTES \
  if(read(fd, &read_count,4) < 0){ \
    O("Read error!\n"); \
    break; \
  }\
read_count = be32toh(read_count);

#define B(x) (1 << x)
#define BITSELECTION(x,...) (1 << x)|BITSELECTION(__VA_ARGS__)

inline int get_mask(int start, int end){
  int returnable = 0;
  while(start <= end){
    returnable |= B(start);
    start++;
  }
  return returnable;
}

int main(int argc, char** argv){
  int fd,i;
  long count = 0;
  long fsize;
  long spacing;
  int read_count;
  struct stat st;
  if(argc < 2){
    O("Usage: %s <filename> \n", argv[0]);
    exit(-1);
  }

  
  if(stat(argv[1], &st) != 0){
    O("error in stat\n");
    exit(-1);
  }

  //spacing = atol(argv[2]);
  spacing = VDIFSIZE;

  fd = open(argv[1], O_RDONLY);
  if(fd == -1){
    O("Error opening file %s\n", argv[1]);
    exit(-1);
  }

  fsize = st.st_size;
  

  /*
  if(read(fd, &count,8) < 0){
    O("Read error!");
    exit(-1);
  }
  count = be64toh(count);
  O("first count is %ld\n", count);
  count++;
  */
  int valid_data, legacy_mode, seconds_from_epoch, epoch, data_frame_num, vdif_version, log2_channels, frame_length, data_type, bits_per_sample, thread_id, station_id;

  for(i=0;i*spacing < fsize;i++){

    lseek(fd, i*spacing, SEEK_SET);

    GRAB_4_BYTES
    valid_data = read_count & B(31);
    legacy_mode = read_count & B(30);
    seconds_from_epoch = read_count & get_mask(0,29);
      /*
    valid_data = 0x80000000 & read_count;
    legacy_mode = 0x40000000 & read_count;
    seconds_from_epoch = 0x3fffffff & read_count;
    */

    GRAB_4_BYTES
    epoch = read_count & get_mask(24,29);
    data_frame_num = read_count & get_mask(0,23);
      /*
    epoch = 0x3e000000 & read_count;
    data_frame_num = 0x01ffffff & read_count;
    */

    GRAB_4_BYTES
    vdif_version = read_count & get_mask(29,31);
    log2_channels = read_count & get_mask(24,28);
    frame_length = read_count & get_mask(0,23);
      /*
    vdif_version = 0xe0000000 & read_count;
    log2_channels = 0x1f000000 & read_count;
    frame_length = 0x00ffffff & read_count;
    */

    GRAB_4_BYTES
    data_type = read_count & B(31);
    bits_per_sample = read_count & get_mask(26,30);
    thread_id = read_count & get_mask(16,25);
    station_id = read_count & get_mask(0,15);
      /*
    data_type = 0x80000000 & read_count;
    bits_per_sample = 0x7c000000 & read_count;
    thread_id = 0x03ff0000 & read_count;
    station_id = 0x0000ffff & read_count;
    */

    fprintf(stdout, "---------------------------------------------------------------------------\n");
    fprintf(stdout, "| valid_data: %5s | legacy_mode: %5s | seconds_from_epoch: %14d |\n", BOLPRINT(valid_data), BOLPRINT(legacy_mode), seconds_from_epoch);
    fprintf(stdout, "| epoch: %14d | data_frame_num: %14d |\n", epoch, data_frame_num);
    fprintf(stdout, "| vdif_version: %2d | log2_channels: %4d | frame_length %14d |\n", vdif_version, log2_channels, frame_length);
    fprintf(stdout, "| data_type: %8s | bits_per_sample: %6d | thread_id: %6d | station_id %8d |\n", DATATYPEPRINT(data_type), bits_per_sample, thread_id, station_id);

    
    /*
    if(count != read_count){
      fprintf(stdout, "Discrepancy as count is %ld and read_count is %ld\n",count, read_count);
      //count = read_count;
    }
    */
    count++;
  }
  fprintf(stdout, "Done!\n");

  if(close(fd) != 0){
    O("Error on close\n");
    exit(-1);
  }

  exit(0);
}
