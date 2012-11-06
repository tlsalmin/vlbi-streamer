#ifndef COMMON_H
#define COMMON_H

#define O(...) fprintf(stdout, __VA_ARGS__)
#define MARK5SIZE 10016
#define VDIFSIZE 8224
#define MARK5NETSIZE 10032
#define MARK5OFFSET 8
#define BOLPRINT(x) (x)?"true":"false"
#define DATATYPEPRINT(x) (x)?"Complex":"Real"
#define GRAB_AND_SHIFT(word,start,end) ((word & get_mask(start,end)) >> start)
#define JUMPSIZE 1000
#define SHIFTCHAR(x) ((((x) & 0x08) >> 3) | (((x) & 0x04) >> 1) | (((x) & 0x02) << 1) | (((x) & 0x01) << 3))
#define B(x) (1 << x)
#define HEXMODE B(0)
#define NETMODE B(1)
#define ISAUTO	B(2)
#define SEEKIT 	B(3)


struct common_control_element{
  int fd;
  long count;
  long fsize;
  int running;
  int framesize;
  int offset;
  int hexoffset;
  void *mmapfile;
  void *target;
  int read_count;
  int optbits;
  struct stat st;
};

unsigned int get_mask(int start, int end);

int getopts(int argc, char **argv, struct common_control_element * cce);

int keyboardinput(struct common_control_element * cce);
//#define GRAB_4_AND_SHIFT(pointer,offset) *((int*)((pointer & get_mask(offset,offset+4)) >> offset))
//#define BITSELECTION(x,...) (1 << x)|BITSELECTION(__VA_ARGS__)
#endif
