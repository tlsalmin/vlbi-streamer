#ifndef METADATA_MARK5B_H
#define METADATA_MARK5B_H
#define POINT_TO_THE_RIGHT_DIRECTION cce->buffer+cce->offset+ms->netoffset

struct metadata_mark5b
{
  int initialized;
  int netoffset;
  int userspecified;
  int tvg;
  /* Setting these long just in case	*/
  int syncword;
  long count;
  long framenum;
  long frames_per_second;
  //long second_of_day;
  int day_of_year;
  int second_of_day;
  long sec_fraction;
  int CRCC;
  long lastmove;
};
void mark5b_cleanup(struct common_control_element* cce)
{
  free(cce->datatype_metadata);
}
void mark5b_metadata_increment(struct common_control_element* cce, long count)
{
  struct metadata_mark5b *ms = cce->datatype_metadata;
  if(ms->frames_per_second != 0)
  {
    ms->second_of_day += (ms->framenum + count)/ms->frames_per_second;
    ms->framenum = (ms->framenum + count) % ms->frames_per_second;
    if(ms->framenum < 0)
      ms->framenum = ms->frames_per_second - ms->framenum;
  }
  else
  {
    ms->framenum += count;
    if(ms->framenum < 0)
      ms->framenum = 0;
  }
  /* If we jump over first packet boundary with 1k jump	we need some help with	*/
  /* remembering our lastmove							*/
  ms->lastmove = count;
}
int check_m5metadata(struct common_control_element*cce)
{
  struct metadata_mark5b* ms = cce->datatype_metadata;
  ms->netoffset = BITUP(cce->optbits, DATATYPE_MARK5BNET) ? 8 : 0;
  if(syncword_check(cce->buffer+ms->netoffset) == 0)
  {
    LOG("First syncword wasnt ABADDEED but %X. Checking next packet\n",*((uint32_t*)POINT_TO_THE_RIGHT_DIRECTION));
    cce->offset = MARK5OFFSET + ms->netoffset;
    if(syncword_check(POINT_TO_THE_RIGHT_DIRECTION) == 0){
      E("Cant find syncword. This is not mark5 data. Syncword was %X",, *((uint32_t*)POINT_TO_THE_RIGHT_DIRECTION));
      return -1;
    }
  }
  ms->initialized = 1;
  return 0;
}
void copy_m5metadata(struct common_control_element *cce, struct metadata_mark5b* ms)
{
  //struct metadata_mark5b * ms = cce->datatype_metadata;
  if(BITUP(cce->optbits, DATATYPE_MARK5BNET))
      ms->count = getseq_mark5b_net(cce->buffer + cce->offset);
  get_sec_and_day_from_mark5b(POINT_TO_THE_RIGHT_DIRECTION, &ms->second_of_day, &ms->day_of_year);
  ms->userspecified = USERSPEC_FROM_MARK5B(POINT_TO_THE_RIGHT_DIRECTION);
  ms->CRCC = CRC_FROM_MARK5B(POINT_TO_THE_RIGHT_DIRECTION);
  ms->framenum = FRAMENUM_FROM_MARK5B(POINT_TO_THE_RIGHT_DIRECTION);
  ms->syncword = *((uint32_t*)(POINT_TO_THE_RIGHT_DIRECTION));
}
int mark5_printmetadata(struct common_control_element *cce)
{
  struct metadata_mark5b *ms = cce->datatype_metadata;
  if(ms->initialized == 0){
    if(check_m5metadata(cce) != 0)
      return -1;
  }
  copy_m5metadata(cce, ms);
  LOG("| Syncword: %5X |\n| Userspecified: %6X\t| tvg %6s\t| Framenum: %6ld|\n| Day_of_year: %6d\t| Second_of_day: %6d|\n|Fraction: 0.%6lX\t| CRCC: %6d\n", ms->syncword, ms->userspecified, BOLPRINT(ms->tvg), ms->framenum, ms->day_of_year, ms->second_of_day, ms->sec_fraction, ms->CRCC);
  return 0;
}
int mark5b_discrepancy(struct common_control_element *cce)
{
  struct metadata_mark5b *ms = cce->datatype_metadata;
  if(ms->initialized == 0){
    LOG("Printing data from first packet\n");
    return mark5_printmetadata(cce);
  }
  copy_m5metadata(cce,ms+1);
  return 0;
}
int init_mark5b_data(struct common_control_element *cce)
{
  struct metadata_mark5b* ms;
  if(cce->optbits & TRAVERSE_CHECK){
    cce->datatype_metadata = malloc(2*sizeof(struct metadata_mark5b));
    ms = cce->datatype_metadata;
    memset(ms, 0, 2*sizeof(struct metadata_mark5b));
  }
  else
  {
    cce->datatype_metadata = malloc(sizeof(struct metadata_mark5b));
    ms = cce->datatype_metadata;
    memset(ms, 0, sizeof(struct metadata_mark5b));
  }
  cce->print_info = mark5_printmetadata;
  cce->cleanup_inspector = mark5b_cleanup;
  cce->metadata_increment = mark5b_metadata_increment;
  cce->check_for_discrepancy = mark5b_discrepancy;
  return 0;
}
#endif
