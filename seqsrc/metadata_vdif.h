#ifndef METADATA_VDIF_H
#define METADATA_VDIF_H

#ifdef LOG_INCONSISTENT_PACKET
#undef LOG_INCONSISTENT_PACKET
#endif
#define LOG_DEFAULT_BROKEN_PACKET LOG_INCONSISTENT_PACKET((ms), (ms+1))
#define LOG_INCONSISTENT_PACKET(x,previous_x) LOG("Previous packet\n");vdif_printcommand(previous_x);LOG("Broken packet\n");vdif_printcommand(x)

struct metadata_vdif
{
  int initialized;
  int valid_data;
  int legacy_mode;
  int32_t sec_from_reference_epoch;
  int32_t reference_epoch;
  int syncword;
  int vdif_version;
  int32_t framenum;
  int log2_num_channels;
  int data_frame_length_8byte_units;
  int real_or_complex;
  int bits_per_sample;
  int thread_id;
  int station_id;
};
void vdif_cleanup(struct common_control_element* cce)
{
  free(cce->datatype_metadata);
}
void copy_vdifmetadata(void *buffer, struct metadata_vdif* ms)
{
  ms->sec_from_reference_epoch = GET_VDIF_SECONDS(buffer);
  ms->framenum = FRAMENUM_FROM_VDIF(buffer);
  ms->valid_data = GET_VALID_BIT(buffer);
  ms->legacy_mode = GET_LEGACY_BIT(buffer);
  ms->vdif_version = GET_VDIF_SECONDS(buffer);
  ms->log2_num_channels = GET_VDIF_CHANNELS(buffer);
  ms->data_frame_length_8byte_units = GET_VDIF_FRAME_LENGTH(buffer);
  ms->real_or_complex = GET_VDIF_DATATYPE(buffer);
  ms->bits_per_sample = GET_VDIF_BITS_PER_SAMPLE(buffer);
  ms->thread_id = GET_VDIF_THREAD_ID(buffer);
  ms->station_id = GET_VDIF_STATION_ID(buffer);
}
void vdif_metadata_increment(struct common_control_element* cce, long count)
{
  (void)count;
  //struct metadata_vdif *ms = cce->datatype_metadata;
  copy_vdifmetadata(cce->buffer, (struct metadata_vdif*)cce->datatype_metadata);
}
void vdif_printcommand(struct metadata_vdif* ms)
{
  LOG("| Valid data: %6s\t| Legacy_mode: %6s\t| second_from_epoch %14d\t| Epoch: %14d|\n", BOLPRINT(ms->valid_data), BOLPRINT(ms->legacy_mode), ms->sec_from_reference_epoch, ms->reference_epoch);
  LOG("| data_frame_num: %14d\t| vdif_version: %2d|log2_channels: %4d\t| frame_length: %14d|\n", ms->framenum, ms->vdif_version, ms->log2_num_channels, ms->data_frame_length_8byte_units);
  LOG("| data_type: %8s\t| bits_per_sample: %6d\t| thread_id %6d\t| station_id %8d|\n", BOLPRINT(ms->real_or_complex), ms->bits_per_sample, ms->thread_id, ms->station_id);
}
int vdif_printmetadata(struct common_control_element *cce)
{
  struct metadata_vdif *ms = cce->datatype_metadata;
  if(cce->initialized == 0){
    copy_vdifmetadata(cce->buffer, ms);
    cce->initialized=1;
  }
  vdif_printcommand(ms);
  return 0;
}
int vdif_discrepancy(struct common_control_element *cce)
{
  struct metadata_vdif *ms = cce->datatype_metadata;
  struct metadata_vdif *prev_ms = ms+1;
  if(cce->initialized == 0){
    int err;
    LOG("Printing data from first packet\n");
    err = vdif_printmetadata(cce);
    memcpy(ms+1, ms, sizeof(struct metadata_vdif));
    return err;
  }
  //copy_m5metadata(cce,ms+1);
  if((cce->packets_per_second == 0) && ((ms+1)->sec_from_reference_epoch != (ms)->sec_from_reference_epoch))
  {
    cce->packets_per_second = prev_ms->framenum+1;
    //LOG_DEFAULT_BROKEN_PACKET;
    LOG("Found first second boundary at second_of_day %d. Packets per second is %ld\n", prev_ms->sec_from_reference_epoch, cce->packets_per_second);
  }
  if((ms)->framenum != (prev_ms->framenum +1))
  {
    if(!((ms->sec_from_reference_epoch == (prev_ms)->sec_from_reference_epoch+1))){
      LOG("Inconsistency as previous framenum was %d and now is %d\n", prev_ms->framenum, (ms)->framenum);
      LOG_DEFAULT_BROKEN_PACKET;
    }
    else
    {
      if(prev_ms->framenum != (cce->packets_per_second-1)){
	LOG_DEFAULT_BROKEN_PACKET;
	LOG("Missing last frame of last packet as packets_per_second is %ld and last frames number was %d\n", cce->packets_per_second, prev_ms->framenum);
      }
    }
  }
  else
  {
    if(prev_ms->sec_from_reference_epoch != ms->sec_from_reference_epoch){
      LOG_DEFAULT_BROKEN_PACKET;
      LOG("Inconsistency as last frames second is %d and now its %d\n", prev_ms->sec_from_reference_epoch, ms->sec_from_reference_epoch);
    }
    if(prev_ms->reference_epoch != ms->reference_epoch){
      LOG_DEFAULT_BROKEN_PACKET;
      LOG("Inconsistency as last frames day is %d and now its %d\n", prev_ms->reference_epoch, ms->reference_epoch);

    }
  }
  memcpy(ms+1, ms, sizeof(struct metadata_vdif));
  /* Copy now frame to previous frame	*/
  return 0;
}
int init_vdif_data(struct common_control_element *cce)
{
  struct metadata_vdif* ms;
  if(cce->optbits & TRAVERSE_CHECK){
    cce->datatype_metadata = malloc(2*sizeof(struct metadata_vdif));
    ms = cce->datatype_metadata;
    memset(ms, 0, 2*sizeof(struct metadata_vdif));
  }
  else
  {
    cce->datatype_metadata = malloc(sizeof(struct metadata_vdif));
    ms = cce->datatype_metadata;
    memset(ms, 0, sizeof(struct metadata_vdif));
  }
  cce->print_info = vdif_printmetadata;
  cce->cleanup_inspector = vdif_cleanup;
  cce->metadata_increment = vdif_metadata_increment;
  cce->check_for_discrepancy = vdif_discrepancy;
  return 0;
}
#endif
