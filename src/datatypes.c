#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "datatypes.h"
#include "streamer.h"
#include "active_file_index.h"
#include "datatypes_common.h"

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
long header_match(void* target, void* match, struct opt_s * opt)
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
int check_and_fill(void * buffer, struct opt_s* opt, long fileid, int *expected_errors)
{
  int i;
  int err;
  int errors=0;
  long match;
  long real_number_of_elements;

  /* Makes sure we only fill as much as we have received */
  /* If wrapper added for unit tests */
  if(opt->fi != NULL)
  {
    long packets_left = get_n_packets(opt->fi) - opt->buf_num_elems*fileid;
    real_number_of_elements = MIN(opt->buf_num_elems, packets_left);
  }
  else
    real_number_of_elements = opt->buf_num_elems;

  void* modelheader = create_initial_header(fileid, opt);
  CHECK_ERR_NONNULL(modelheader, "Create modelheader");
  for(i=0;i<real_number_of_elements;i++)
  {
    match = header_match(buffer, modelheader, opt);
    if(match != 0)
    {
      D("A hole to fill found!. Match missed by %ld",, match);
      err = fillpattern(buffer, modelheader,opt);
      CHECK_ERR("Fillpattern");
      errors++;
    }
    err =  increment_header(modelheader,opt->optbits);
    //CHECK_ERR("increment header");
    buffer += opt->packet_size;
  }
  /* Expected errors not yet used */
  if(expected_errors != NULL)
    *expected_errors = errors;
  D("Check and fill showed %d holes",, errors);
  free(modelheader);
  return 0;
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
      return secdiff_from_mark5b(buffer, time, res_err);
      break;
    case DATATYPE_UDPMON:
      E("Udpmon doesn't have timestamp!");
      if(res_err != NULL)
	*res_err = -1;
      return 0;
      break;
    case DATATYPE_MARK5BNET:
      return secdiff_from_mark5b_net(buffer,time,res_err);
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
  /* Checking if diff is more than 68 years?! What was i thinking.. 	*/
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
