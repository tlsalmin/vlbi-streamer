#ifndef VBS_METADATA_H
#define VBS_METADATA_H

#ifndef B
#define B(x) (1 << x)
#endif

#define MIN(x,y) (x < y ? x : y)
#define BITUP(x,y) (((x) & (y)) ? 1 : 0)
#define MEG			B(20)
#define GIG			B(30)

#define MAX_FRAMESIZE			65536
#define MAX_WORDSIZE			16

#define LOCKER_LISTEN			0x00000f00
#define LISTEN_TCPSOCKET 		B(8)
#define LISTEN_UDPSOCKET 		B(9)
#define LISTEN_FILE 			B(10)

#define LOCKER_TRAVERSE			0x0000f000
#define TRAVERSE_VIM			B(12)
#define TRAVERSE_CHECK			B(13)
#define TRAVERSE_OUTPUT			B(14)

#define HEXMODE				B(16)
#define CHANGE_ENDIANESS		B(17)

struct common_control_element{
  int fd;
  long optbits;
  char *port_or_filename;
  //long fsize;
  //int running;
  int sockfd;
  int framesize;
  void* buffer;
  int offset;
  int hexoffset;
  //int(*read_first_packet)(struct common_control_element*);
  int(*packet_move)(int , void * , int, int );
  void(*metadata_increment)(struct common_control_element*, long);
  int(*print_info)(struct common_control_element*);
  int(*check_for_discrepancy)(struct common_control_element*);
  void(*cleanup_reader)(struct common_control_element*);
  void(*cleanup_inspector)(struct common_control_element*);
  void(*reset_to_last_known_good)(struct common_control_element *);
  int read_count;
  uint64_t errors;
  uint64_t max_errors;
  long packets_per_second;
  int initialized;
  void * datatype_metadata;
  void * listen_metadata;
  void * traverse_metadata;
  int running;
};

unsigned int get_mask(int start, int end);
int getopts(int argc, char **argv, struct common_control_element * cce);
int keyboardinput(struct common_control_element * cce);
#endif
