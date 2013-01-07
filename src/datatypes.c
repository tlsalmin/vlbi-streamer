#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "datatypes.h"
#include "streamer.h"


inline long getseq_vdif(void* header, struct resq_info *resq){
  long returnable;
  long second = SECOND_FROM_VDIF(header);
  long framenum = FRAMENUM_FROM_VDIF(header);
  //D("Dat second %lu, dat framenum %lu",, second, framenum);
  if(resq->starting_second == -1){
    //D("Got first second as %lu, framenum %lu",, second, framenum);
    resq->starting_second =  second;
    return framenum;
  }
  if(resq->packets_per_second == -1){
    if(second == resq->current_seq)
      return framenum;
    else{
      //D("got packets per seconds as %lu",, resq->current_seq);
      resq->packets_per_second = resq->current_seq;
      resq->packetsecdif = second - resq->starting_second;
    }
  }
  
  if(resq->packets_per_second == 0){
    returnable = (second - resq->starting_second)/resq->packetsecdif;
  }
  else
    returnable =  (second - (long)resq->starting_second)*((long)resq->packets_per_second) + framenum;
  //D("Returning %lu",, returnable);
  return returnable;
}
int copy_metadata(void* target, void* source, struct opt_s* opt)
{
  switch(opt->optbits & LOCKER_DATATYPE)
  {
    case DATATYPE_VDIF:
      memcpy(target, source, HSIZE_VDIF);
      break;
    case DATATYPE_MARK5B:
      memcpy(target,source, HSIZE_MARK5B);
      break;
    case DATATYPE_UDPMON:
      memcpy(target,source, HSIZE_UDPMON);
      break;
    case DATATYPE_MARK5BNET:
      memcpy(target,source, HSIZE_MARK5BNET);
      break;
    default:
      E("Unknown datatype");
      return -1;
  }
  return 0;
}
int init_header(void** target, struct opt_s* opt)
{
  switch(opt->optbits & LOCKER_DATATYPE)
  {
    case DATATYPE_VDIF:
      *target = malloc(HSIZE_VDIF);
      break;
    case DATATYPE_MARK5B:
      /* Header is 4 words long */
      *target = malloc(HSIZE_MARK5B);
      break;
    case DATATYPE_UDPMON:
      /* 64-bit psn */
      *target = malloc(HSIZE_UDPMON);
      break;
    case DATATYPE_MARK5BNET:
      /* 32-bit psn + 32bit filler */
      *target = malloc(HSIZE_MARK5BNET);
      break;
    default:
      E("Unknown datatype");
      return -1;
  }
  return 0;
}
inline long getseq_mark5b_net(void* header){
  //return be32toh(*((long*)(header+4)));
  return (long)(*((int*)(header+4)));
}
inline long getseq_udpmon(void* header){
  return be64toh(*((long*)header));
}
inline long header_match(void* target, void* match, struct opt_s * opt)
{
  switch(opt->optbits & LOCKER_DATATYPE)
  {
    case DATATYPE_VDIF:
      return (getseq_vdif(target, opt->resqut), getseq_vdif(match, opt->resqut));
      break;
    case DATATYPE_MARK5B:
      //TODO!
      return 0;
      break;
    case DATATYPE_UDPMON:
      return (getseq_udpmon(target) - getseq_udpmon(match));
      break;
    case DATATYPE_MARK5BNET:
      return (getseq_mark5b_net(target) - getseq_mark5b_net(match));
      break;
    default:
      E("Unknown datatype");
      return -1;
  }
}
void * create_initial_header(long fileid, struct opt_s *opt)
{
  void* header = NULL;
  int err;
  struct resq_info * resq = (struct resq_info*)opt->resqut;
  if(opt->first_packet == NULL){
    E("First packet not received yet!");
    return NULL;
  }
  err  = init_header(&header, opt);
  if(header == NULL)
    return NULL;
  long * hdr = (long*)header;
  switch(opt->optbits & LOCKER_DATATYPE)
  {
    case DATATYPE_VDIF:
      //*header = getseq_vdif(opt->first_packet,opt->resq) + opt->buf_num_elems * fileid;
      if(resq == NULL || resq->starting_second == -1  || resq->packets_per_second == -1){
	E("resq info not ready yet. Can't calc header stuff");
	free(header);
	return NULL;
      }
      memcpy(header, opt->first_packet, HSIZE_VDIF);
      //TODO
      break;
    case DATATYPE_MARK5B:
      if(resq == NULL){
	E("resq info not in opt yet. Can't calc header stuff");
	free(header);
	return NULL;
      }
      memcpy(header, opt->first_packet, HSIZE_MARK5B);
      //TODO
      break;
    case DATATYPE_UDPMON:
      memset(header,0,HSIZE_UDPMON);
      *hdr = be64toh(getseq_udpmon(opt->first_packet) + opt->buf_num_elems * fileid);
      break;
    case DATATYPE_MARK5BNET:
      memset(hdr,0,HSIZE_MARK5BNET);
      *hdr = getseq_mark5b_net(opt->first_packet) + opt->buf_num_elems * fileid;
      if(*hdr & 0xffffffff00000000){
	E("Mark5bnet header should have 4 bytes of fillers at start. Must have gone around. %lX",, *hdr);
	*hdr = *hdr & 0x00000000ffffffff;
      }
      break;
    default:
      E("Unknown datatype");
      return NULL;
  }
  D("Setting header to %lX",, *hdr);
  return header;
}
int fillpattern(void * buffer, void * modelheader,struct opt_s* opt)
{
  /* TODO: Fillpattern standars */
  switch(opt->optbits & LOCKER_DATATYPE)
  {
    case DATATYPE_VDIF:
      memcpy(buffer,modelheader,HSIZE_VDIF);
      break;
    case DATATYPE_MARK5B:
      memcpy(buffer,modelheader,HSIZE_MARK5B);
      break;
    case DATATYPE_UDPMON:
      memcpy(buffer,modelheader,HSIZE_UDPMON);
      break;
    case DATATYPE_MARK5BNET:
      memcpy(buffer,modelheader,HSIZE_MARK5BNET);
      break;
    default:
      E("Unknown datatype");
      return -1;
  }
  return 0;
}
int increment_header(void * modelheader, struct opt_s* opt)
{
  /* TODO: Fillpattern standars */
  switch(opt->optbits & LOCKER_DATATYPE)
  {
    case DATATYPE_VDIF:
      //memcpy(buffer,modelheader,HSIZE_VDIF
      break;
    case DATATYPE_MARK5B:
      //memcpy(buffer,modelheader,HSIZE_MARK5B
      break;
    case DATATYPE_UDPMON:
      *((long*)modelheader) = be64toh(getseq_udpmon(modelheader) + 1 );
      break;
    case DATATYPE_MARK5BNET:
      *((long*)modelheader) = be64toh(getseq_mark5b_net(modelheader) + 1 );
      break;
    default:
      E("Unknown datatype");
      return -1;
  }
  return 0;
}
int check_and_fill(void * buffer, struct opt_s* opt, long fileid, int *expected_errors)
{
  int i;
  int err;
  int errors=0;
  long match;
  void* modelheader = create_initial_header(fileid, opt);
  CHECK_ERR_NONNULL(modelheader, "Create modelheader");
  for(i=0;i<opt->buf_num_elems;i++)
  {
    match = header_match(buffer, modelheader, opt);
    if(match != 0)
    {
      D("A hole to fill found!. Match missed by %ld",, match);
      err = fillpattern(buffer, modelheader,opt);
      CHECK_ERR("Fillpattern");
      errors++;
    }
    err =  increment_header(modelheader,opt);
    //CHECK_ERR("increment header");
    buffer += opt->packet_size;
  }
  /* Expected errors not yet used */
  if(expected_errors != NULL)
    *expected_errors = errors;
  D("Check and fill showed %d holes",, errors);
  return 0;
}

