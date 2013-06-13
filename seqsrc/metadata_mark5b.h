#ifndef METADATA_MARK5B_H
#define METADATA_MARK5B_H
#define POINT_TO_THE_RIGHT_DIRECTION cce->buffer+cce->offset+ms->netoffset


#ifdef LOG_INCONSISTENT_PACKET
#undef LOG_INCONSISTENT_PACKET
#endif
#define LOG_INCONSISTENT_PACKET(x,previous_x) LOG("Previous packet\n");mark5_printcommand(previous_x);LOG("Broken packet\n");mark5_printcommand(x)
#define LOG_DEFAULT_BROKEN_PACKET LOG_INCONSISTENT_PACKET((ms), (ms+1))
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
  int m5netframenum;
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
  if(cce->optbits & DATATYPE_MARK5BNET)
    ms->m5netframenum = NETFRAMENUM_FROM_MARK5BNET(cce->buffer+cce->offset);
  else
    ms->m5netframenum = -1;
}
void mark5b_metadata_increment(struct common_control_element* cce, long count)
{
  /*
  struct metadata_mark5b *ms = cce->datatype_metadata;
  if(cce->packets_per_second != 0)
  {
    ms->second_of_day += (ms->framenum + count)/cce->packets_per_second;
    ms->framenum = (ms->framenum + count) % cce->packets_per_second;
    if(ms->framenum < 0)
      ms->framenum = cce->packets_per_second - ms->framenum;
  }
  else
  {
    ms->framenum += count;
    if(ms->framenum < 0)
      ms->framenum = 0;
  }
  ms->lastmove = count;
  */
  (void)count;
  struct metadata_mark5b *ms = cce->datatype_metadata;
  copy_m5metadata(cce, ms);
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
  cce->initialized = 1;
  copy_m5metadata(cce, ms);
  return 0;
}
void mark5_printcommand(struct metadata_mark5b* ms)
{
  if(ms->m5netframenum != -1)
    LOG("| M5NetFrameNum: %d ", ms->m5netframenum);
  LOG("| Syncword: %5X |\n| Userspecified: %6X\t| tvg %6s\t| Framenum: %6ld|\n| Day_of_year: %6d\t| Second_of_day: %6d|\n|Fraction: 0.%6lX\t| CRCC: %6d\n", ms->syncword, ms->userspecified, BOLPRINT(ms->tvg), ms->framenum, ms->day_of_year, ms->second_of_day, ms->sec_fraction, ms->CRCC);
}
int mark5_printmetadata(struct common_control_element *cce)
{
  struct metadata_mark5b *ms = cce->datatype_metadata;
  if(cce->initialized == 0){
    if(check_m5metadata(cce) != 0)
      return -1;
  }
  //copy_m5metadata(cce, ms);
  mark5_printcommand(ms);
  return 0;
}
int mark5b_discrepancy(struct common_control_element *cce)
{
  struct metadata_mark5b *ms = cce->datatype_metadata;
  struct metadata_mark5b *prev_ms = ms+1;
  if(cce->initialized == 0){
    int err;
    LOG("Printing data from first packet\n");
    err = mark5_printmetadata(cce);
    memcpy(ms+1, ms, sizeof(struct metadata_mark5b));
    return err;
  }
  //copy_m5metadata(cce,ms+1);
  if((cce->packets_per_second == 0) && ((ms+1)->second_of_day != (ms)->second_of_day))
  {
    cce->packets_per_second = prev_ms->framenum+1;
    //LOG_DEFAULT_BROKEN_PACKET;
    LOG("Found first second boundary at second_of_day %d. Packets per second is %ld\n", prev_ms->second_of_day, cce->packets_per_second);
  }
  if(cce->optbits & DATATYPE_MARK5BNET)
  {
    /* Its plus 2 here, since were hopping whole mark5 frames and each of them consists of 2 mark5bnet frames	*/
    if(ms->m5netframenum != prev_ms->m5netframenum+2)
    {
      LOG("Inconsistency as mark5bnet framenum was %d and now is %d\n", prev_ms->m5netframenum, ms->m5netframenum);
      LOG_DEFAULT_BROKEN_PACKET;
    }
  }
  /* If we have all we need for full metadata checking 	*/
  if((ms)->framenum != (prev_ms->framenum +1))
  {
    /* Check if we just changed seconds	*/
    if(!((ms->second_of_day == (prev_ms)->second_of_day+1))){
      LOG("Inconsistency as previous framenum was %ld and now is %ld\n", prev_ms->framenum, (ms)->framenum);
      LOG_DEFAULT_BROKEN_PACKET;
    }
    else
    {
      if(prev_ms->framenum != (cce->packets_per_second-1)){
	LOG_DEFAULT_BROKEN_PACKET;
	LOG("Missing last frame of last packet as packets_per_second is %ld and last frames number was %ld\n", cce->packets_per_second, prev_ms->framenum);
      }
    }
  }
  else
  {
    if(prev_ms->second_of_day != ms->second_of_day){
      LOG_DEFAULT_BROKEN_PACKET;
      LOG("Inconsistency as last frames second is %d and now its %d\n", prev_ms->second_of_day, ms->second_of_day);
    }
    if(prev_ms->day_of_year != ms->day_of_year){
      LOG_DEFAULT_BROKEN_PACKET;
      LOG("Inconsistency as last frames day is %d and now its %d\n", prev_ms->day_of_year, ms->day_of_year);

    }
  }
  if(cce->optbits & TRAVERSE_CHECK)
    memcpy(ms+1, ms, sizeof(struct metadata_mark5b));
  /* Copy now frame to previous frame	*/
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
