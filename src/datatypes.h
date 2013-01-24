#ifndef DATATYPES_H
#define DATATYPES_H
#define BITMASK_30 0xfffffff3
#define BITMASK_24 0xffffff00
#define BITMASK_12 0xfff00000
#define RBITMASK_30 0x3fffffff
#define RBITMASK_24 0x00ffffff
#define RBITMASK_20 0x000fffff

#define MARK5BSYNCWORD 0xABADDEED

#define SECONDS_IN_DAY 86400
/* A threashold of 2.5h should be enough 		*/
#define MIDNIGHTRESHOLD 10000

/* Used when we identify a later half of a mark5b frame */
#define NONEVEN_PACKET	INT32_MAX
#define ERROR_IN_DIFF	INT32_MIN
#define SEC_OF_DAY_FROM_TM(x) (60*60*(x)->tm_hour + 60*(x)->tm_min + (x)->tm_sec)

/* Really VDIF could be 32 bytes, but we won't care about extended user stuff anyway */
#define HSIZE_VDIF 16
#define HSIZE_MARK5B 16
#define HSIZE_UDPMON 8
#define HSIZE_MARK5BNET 24
#define POINT_TO_MARK5B_SECOND(x) ((x)+4+4)

#define FRAMENUM_FROM_VDIF(x) (long)(*((uint32_t*)((x)+4)) & RBITMASK_24)
#define SET_FRAMENUM_FOR_VDIF(target,framenum) *((uint32_t*)(target+4)) = framenum & RBITMASK_24
#define SECOND_FROM_VDIF(x) (long)(*((uint32_t*)(x))) & RBITMASK_30;
#define SET_SECOND_FOR_VDIF(target,second) *((uint32_t*)(target)) = second & RBITMASK_30

#define SECOND_FROM_MARK5B(x) (long)((*((uint32_t*)(x+4+4))) & RBITMASK_20);
#define SECOND_FROM_MARK5B_CHAR(x) (long)atoi((*((uint32_t*)(x+4+4))) & RBITMASK_20);
#define DAY_FROM_MARK5B(x) (long)(((*((uint32_t*)(x+4+4))) & BITMASK_12) >> 20);
#define DAY_FROM_MARK5B_CHAR(x) (long)(((*((uint32_t*)(x+4+4))) & BITMASK_12) >> 20);

#define SET_FRAMENUM_FOR_UDPMON(target,framenum) *((uint64_t*)(target)) = be64toh((uint64_t)(framenum));
#define SET_FRAMENUM_FOR_MARK5BNET(target,framenum) *((uint32_t*)(target+4)) = (uint32_t)(framenum);
#define SET_FRAMENUM_FOR_MARK5B(target,framenum) *((uint32_t*)(target)) = (uint32_t)(framenum) & get_mask(0,14);

#include <time.h>
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
inline long getseq_vdif(void* header, struct resq_info *resq);
inline long getseq_mark5b_net(void* header);
inline long getseq_udpmon(void* header);
int copy_metadata(void* source, void* target, struct opt_s* opt);
int init_header(void** target, struct opt_s* opt);
int check_and_fill(void * buffer, struct opt_s* opt, long fileid, int *expected_errors);
inline long header_match(void* target, void* match, struct opt_s * opt);
int get_day_from_mark5b(void *buffer);
int get_sec_from_mark5b(void *buffer);
int get_sec_and_day_from_mark5b(void *buffer, int * sec, int * day);
int get_sec_and_day_from_mark5b_net(void *buffer, int * sec, int * day);
int get_sec_and_day_from_vdif(void *buffer, int * sec, int * day);
long epochtime_from_mark5b(void *buffer, struct tm* reftime);
long epochtime_from_vdif(void *buffer, struct tm* reftime);
long epochtime_from_mark5b_net(void *buffer, struct tm* reftime);
int get_sec_dif_from_buf(void * buffer, struct tm* time,struct opt_s* opt, int* res_err);
int secdiff_from_mark5b_net(void *buffer, struct tm* reftime, int* errres);
int secdiff_from_mark5b(void *buffer, struct tm* reftime, int* errres);

#endif
