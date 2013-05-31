#ifndef DATATYPES_H
#define DATATYPES_H

//#include <time.h>
#include "datatypes_common.h"
#include "streamer.h"

struct resq_info{
  long  *inc_before, *inc;
  void  *buf, *usebuf, *bufstart, *bufstart_before;
  struct buffer_entity * before;
  long current_seq;
  long seqstart_current;
  int i;
  int packets_per_second;
  /* Special if the packets are spaced for example every */
  /* fifth second.					*/
  int packetsecdif;
  struct tm tm_s;
  int starting_second;
};
long getseq_vdif(void* header, struct resq_info *resq);
long getseq_mark5b_net(void* header);
long getseq_udpmon(void* header);
int copy_metadata(void* source, void* target, struct opt_s* opt);
int init_header(void** target, struct opt_s* opt);
int check_and_fill(void * buffer, struct opt_s* opt, long fileid, int *expected_errors);
inline long header_match(void* target, void* match, struct opt_s * opt);
int get_sec_dif_from_buf(void * buffer, struct tm* time,struct opt_s* opt, int* res_err);

#endif
