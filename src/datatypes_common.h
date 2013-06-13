#ifndef DATATYPES_COMMON_H
#define DATATYPES_COMMON_H

#define BITMASK_30 0xfffffff3
#define BITMASK_24 0xffffff00
#define BITMASK_16 0xffff0000
#define BITMASK_12 0xfff00000
#define RBITMASK_30 0x3fffffff
#define RBITMASK_24 0x00ffffff
#define RBITMASK_20 0x000fffff
#define BITMASK_16_REV 0x0000ffff
#define BITMASK_15_REV 0x00007fff

#define MARK5BSYNCWORD 0xABADDEED

#ifndef B
#define B(x) (1l << x)
#endif

#define SECONDS_IN_DAY 86400
/* A threashold of 2.5h should be enough 		*/
#define MIDNIGHTRESHOLD 10000

/* Used when we identify a later half of a mark5b frame */
#define NONEVEN_PACKET	INT32_MAX
#define ERROR_IN_DIFF	INT32_MIN
#define SEC_OF_DAY_FROM_TM(x) (60*60*(x)->tm_hour + 60*(x)->tm_min + (x)->tm_sec)

#define END_OF_FD -INT_MAX

#define LOCKER_DATATYPE		0x000000ff00000000
#define	DATATYPE_UNKNOWN	B(32) 
#define	DATATYPE_VDIF		B(33) 
#define	DATATYPE_MARK5B		B(34) 
#define DATATYPE_UDPMON		B(35)

#define	DATATYPE_MARK5BNET	B(36) 
/* Next three empty */

/* Really VDIF could be 32 bytes, but we won't care about extended user stuff anyway */
#define HSIZE_VDIF 16
#define HSIZE_MARK5B 16
#define HSIZE_UDPMON 8
#define HSIZE_MARK5BNET 24
#define POINT_TO_MARK5B_SECOND(x) ((x)+4+4)
#define POINT_TO_MARK5B_SECONDWORD(x) ((x)+4)
#define POINT_TO_MARK5B_SECFRAQ(x) ((x)+4+4+4)

#define BOLPRINT(x) (x)?"true":"false"
#define DATATYPEPRINT(x) (x)?"Complex":"Real"
#define GRAB_AND_SHIFT(word,start,end) ((word & get_mask(start,end)) >> start)
#define JUMPSIZE 1000
#define SHIFTCHAR(x) ((((x) & 0x08) >> 3) | (((x) & 0x04) >> 1) | (((x) & 0x02) << 1) | (((x) & 0x01) << 3))

#define MARK5SIZE 10016
#define MARK5NETSIZE 10032
#define MARK5OFFSET 5008
#define MARK5NETOFFSET 5016
#define VDIFSIZE 8224

#define VDIF_SECOND_BITSHIFT 24
/* Grab the last 30 bits of the first 4 byte word 	*/
#define GET_VDIF_SECONDS(buf) (0x3fffffff & be32toh(*((uint32_t*)(buf))))
#define GET_VDIF_REF_EPOCH(buf) ((0x3f000000 & be32toh(*((uint32_t*)(buf))))>>24)
#define GET_VDIF_SSEQ(buf) (0x00ffffff & be32toh(*(((uint32_t*)(buf))+1)))
#define GET_VDIF_VERSION(buf) ((0xE0000000 & be32toh(*(((uint32_t*)(buf))+2)))>>29)
#define GET_VDIF_CHANNELS(buf) ((0x1F000000 & be32toh(*(((uint32_t*)(buf))+2)))>>24)
#define GET_VDIF_FRAME_LENGTH(buf) ((0x00FFFFFF & be32toh(*(((uint32_t*)(buf))+2))))
#define GET_VDIF_DATATYPE(buf) ((0x80000000 & be32toh(*(((uint32_t*)(buf))+3)))>>31)
#define GET_VDIF_BITS_PER_SAMPLE(buf) ((0xEC000000 & be32toh(*(((uint32_t*)(buf))+3)))>>26)
#define GET_VDIF_THREAD_ID(buf) ((0x03FF0000 & be32toh(*(((uint32_t*)(buf))+3)))>>16)
#define GET_VDIF_STATION_ID(buf) ((0x0000FFFF & be32toh(*(((uint32_t*)(buf))+3)))>>16)
#define GET_VALID_BIT(buf) ((0x80000000 & be32toh(*((uint32_t*)buf))) >> 31)
#define GET_LEGACY_BIT(buf) ((0x40000000 & be32toh(*((uint32_t*)buf))) >> 30)
#define FRAMENUM_FROM_VDIF(x) (be32toh(*(((uint32_t*)x)+1)) & RBITMASK_24)

#define SET_FRAMENUM_FOR_VDIF(target,framenum) *((uint32_t*)(target+4)) = framenum & RBITMASK_24
#define SECOND_FROM_VDIF(x) (int64_t)(*((uint32_t*)(x))) & RBITMASK_30;
#define SET_SECOND_FOR_VDIF(target,second) *((uint32_t*)(target)) = second & RBITMASK_30

#define SECOND_FROM_MARK5B(x) ((*((uint32_t*)(POINT_TO_MARK5B_SECOND(x)))) & RBITMASK_20)
#define DAY_FROM_MARK5B(x) (((*((uint32_t*)(POINT_TO_MARK5B_SECOND(x)))) & BITMASK_12) >> 20)
#define USERSPEC_FROM_MARK5B(x) (((*((uint32_t*)(POINT_TO_MARK5B_SECONDWORD(x)))) & BITMASK_16) >> 16)
#define SECFRAQ_FROM_MARK5B(x) (((*((uint32_t*)(POINT_TO_MARK5B_SECFRAQ(x)))) & BITMASK_16) >> 16)
#define CRC_FROM_MARK5B(x) (((*((uint32_t*)(POINT_TO_MARK5B_SECFRAQ(x)))) & BITMASK_16_REV))
#define FRAMENUM_FROM_MARK5B(x) (((*((uint32_t*)(POINT_TO_MARK5B_SECONDWORD(x)))) & BITMASK_15_REV))
#define NETFRAMENUM_FROM_MARK5BNET(x) (((*((uint32_t*)(x+4)))))

#define SET_FRAMENUM_FOR_UDPMON(target,framenum) *((uint64_t*)(target)) = be64toh((uint64_t)(framenum));
#define SET_FRAMENUM_FOR_MARK5BNET(target,framenum) *((uint32_t*)(target+4)) = (uint32_t)(framenum);
#define SET_FRAMENUM_FOR_MARK5B(target,framenum) *((uint32_t*)(target)) = (uint32_t)(framenum) & get_mask(0,14);

#include <time.h>
#include <stdint.h>

struct resq_info{
  unsigned long  *inc_before, *inc;
  void  *buf, *usebuf, *bufstart, *bufstart_before;
  /* A buffer entity before the current			*/
  void * before;
  long current_seq;
  long seqstart_current;
  int i;
  int packets_per_second;
  /* Special if the packets are spaced for example 	*/
  /* every fifth second.				*/
  int packetsecdif;
  struct tm tm_s;
  int starting_second;
};

int copy_metadata(void* source, void* target, uint64_t);
long getseq_vdif(void* header, struct resq_info *resq);
int get_day_from_mark5b(void *buffer);
int get_sec_from_mark5b(void *buffer);
int get_sec_and_day_from_mark5b(void *buffer, int * sec, int * day);
int get_sec_and_day_from_mark5b_net(void *buffer, int * sec, int * day);
int get_sec_and_day_from_vdif(void *buffer, int * sec, int * day);
long epochtime_from_mark5b(void *buffer, struct tm* reftime);
long epochtime_from_vdif(void *buffer, struct tm* reftime);
long epochtime_from_mark5b_net(void *buffer, struct tm* reftime);
int secdiff_from_mark5b_net(void *buffer, struct tm* reftime, int* errres);
int secdiff_from_mark5b(void *buffer, struct tm* reftime, int* errres);
long getseq_mark5b_net(void* header);
uint64_t getseq_udpmon(void* header);
uint64_t get_a_count(void * buffer, int wordsize, int offset, int do_be64toh);
int increment_header(void * modelheader, uint64_t datatype);
uint64_t get_datatype_from_string(char * match);
int syncword_check(void *buffer);
#endif
