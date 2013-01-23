#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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
  if(header == NULL || err != 0){
    E("Error in initializing header");
    return NULL;
  }
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
      //memset(header,0,HSIZE_UDPMON);
      //*hdr = be64toh(getseq_udpmon(opt->first_packet) + opt->buf_num_elems * fileid);
      SET_FRAMENUM_FOR_UDPMON(header, getseq_udpmon(opt->first_packet)+(opt->buf_num_elems)*fileid);
      break;
    case DATATYPE_MARK5BNET:
      //memset(hdr,0,HSIZE_MARK5BNET);
      memcpy(header,opt->first_packet, HSIZE_MARK5BNET);
      SET_FRAMENUM_FOR_MARK5BNET(header, getseq_mark5b_net(opt->first_packet)+(opt->buf_num_elems)*fileid);
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
  switch(opt->optbits & LOCKER_DATATYPE)
  {
    case DATATYPE_VDIF:
      //memcpy(buffer,modelheader,HSIZE_VDIF
      break;
    case DATATYPE_MARK5B:
      //memcpy(buffer,modelheader,HSIZE_MARK5B
      break;
    case DATATYPE_UDPMON:
      SET_FRAMENUM_FOR_UDPMON(modelheader,getseq_udpmon(modelheader)+1);
      break;
    case DATATYPE_MARK5BNET:
      SET_FRAMENUM_FOR_MARK5BNET(modelheader, getseq_mark5b_net(modelheader)+1);
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
int get_sec_from_mark5b(void *buffer)
{
  int day,sec;
  int n;
  if((n=sscanf((char*)POINT_TO_MARK5B_SECOND(buffer), "%03d%05d", &day, &sec)) != 2){
    E("sscanf got only %d from buffer",, n);
    return -1;
  }
  return sec;
}
int get_day_from_mark5b(void *buffer)
{
  int day,sec;
  int n;
  if((n=sscanf((char*)POINT_TO_MARK5B_SECOND(buffer), "%03d%05d", &day, &sec)) != 2){
    E("sscanf got only %d from buffer",, n);
    return -1;
  }
  return day;
}
int get_sec_and_day_from_mark5b(void *buffer, int * sec, int * day)
{
  return sscanf((char*)POINT_TO_MARK5B_SECOND(buffer), "%03d%05d", day, sec);
}
int get_sec_and_day_from_mark5b_net(void *buffer, int * sec, int * day)
{
  return get_sec_and_day_from_mark5b(buffer+8, sec, day);
}
long epochtime_from_mark5b(void *buffer, struct tm* reftime)
{
  /* Presumes that reftime has the same stuff mark5b metadata has */
  int m5seconds, m5days;
  int err;
  err = get_sec_and_day_from_mark5b(buffer, &m5seconds, &m5days);
  if(err != 2)
  {
    E("Didnt't get day and sec from sscanf. Got %d",, err);
    return -1;
  }
  if(m5days != reftime->tm_yday){
    D("Different day in mark5data: in mark5b: %d, right now %d. Better just wait than to try to count this..",, m5days, reftime->tm_yday);
    return DIFFERENT_DAY;
  }
  struct tm m5tm;
  memset(&m5tm, 0, sizeof(struct tm));
  m5tm.tm_hour = m5seconds / (60*60);
  m5tm.tm_min = (m5seconds % (60*60))/60;
  m5tm.tm_sec = ((m5seconds % (60*60)) % 60);
  m5tm.tm_year = reftime->tm_year;
  m5tm.tm_mon = reftime->tm_mon;
  m5tm.tm_mday = reftime->tm_mday;

  return (long)mktime(&m5tm) - timezone;
}
long epochtime_from_vdif(void *buffer, struct tm* reftime)
{
  (void)buffer;
  (void)reftime;

  return 0;
}
long epochtime_from_mark5b_net(void *buffer, struct tm* reftime)
{
  if(*((long*)(buffer+8)) != MARK5BSYNCWORD)
    return NONEVEN_PACKET;
  else
    return epochtime_from_mark5b(buffer+8, reftime);
}
/* Get a difference between (sec from epoch) time and value in buffer 	*/
/* Value type in buffer determined by opt				*/
int get_sec_dif_from_buf(void * buffer, struct tm* time,struct opt_s* opt, int* res_err)
{
  long time2=0,temp;
  int dif = 0;
  if(res_err != NULL)
    *res_err =0;
  switch (opt->optbits & LOCKER_DATATYPE)
  {
    case DATATYPE_VDIF:
      E("Not implemented yet! epcoh 2000 6 months period bull");
      time2 = SECOND_FROM_VDIF(buffer);
      break;
    case DATATYPE_MARK5B:
      time2= epochtime_from_mark5b(buffer, time);
      break;
    case DATATYPE_UDPMON:
      break;
    case DATATYPE_MARK5BNET:
      time2= epochtime_from_mark5b(buffer, time);
      //temp = (mktime(time)-timezone) - time2;
      break;
    case DATATYPE_UNKNOWN:
      E("Can't determine metadata second for unknown");
      if(res_err != NULL)
	*res_err = -1;
      return 0;
      break;
    default: 
      E("Unknown datatype");
      if(res_err != NULL)
	*res_err = -1;
      return 0;
      break;
  }
  temp = GETSECONDS(opt->starting_time) - time2;
  if(temp > INT32_MAX )
  {
    E("Int overflow. Return INT32_MAX");
    dif = INT32_MAX;
  }
  else if(temp<INT32_MIN)
  {
    E("Int overflow. Return INT32_MIN");
    dif = INT32_MIN;
  }
  else
    dif = (int)temp;
  return dif;
}

