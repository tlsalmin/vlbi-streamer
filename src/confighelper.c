/*
 * confighelper.c -- Helper functions on libconfig for vlbi-streamer
 *
 * Written by Tomi Salminen (tlsalmin@gmail.com)
 * Copyright 2012 Metsähovi Radio Observatory, Aalto University.
 * All rights reserved
 * This file is part of vlbi-streamer.
 *
 * vlbi-streamer is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * vlbi-streamer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with vlbi-streamer.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */
#include <config.h>
#include <string.h>
#include <stdlib.h>
#include "streamer.h"
#include "resourcetree.h"
#include "confighelper.h"
#include "active_file_index.h"
#include "config.h"

extern FILE* logfile;

int write_cfgs_to_disks(struct opt_s *opt){
  if(opt->optbits & READMODE)
    oper_to_all(opt->diskbranch, BRANCHOP_READ_CFGS, opt);
  else
    oper_to_all(opt->diskbranch, BRANCHOP_WRITE_CFGS, opt);
  return 0;
}
#ifdef HAVE_LIBCONFIG_H
/* Set all the variables to opt from root. If check is set, then just	*/
/* check the variables against options in opt and return -1 if there	*/
/* is a discrepancy. If write is 1, the option is written to the cfg	*/
/* instead for being read from the cfg					*/
int set_from_root(struct opt_s * opt, config_setting_t *root, int check, int write){
  D("Option root parse, check: %d, write %d",,check,write);
  config_setting_t * setting;
  int err=0,index=0,filesize_found=0;
  /* filesize is deprecated! */
  unsigned long filesize = 0;
  /* If no root specified, use opt->cfg root */
  if(root == NULL)
    root = config_root_setting(&(opt->cfg));

  setting = config_setting_get_elem(root,index);

  while(setting != NULL){
    /* Have to make this a huge if else since its string matching */
    if(strcmp(config_setting_name(setting), "filesize") == 0){
      if(config_setting_type(setting) != CONFIG_TYPE_INT64){
	E("Filesize not int64");
	return -1;
      }
      /* Check for same filesize. Now this loops has to have been performed	*/
      /* once before we try to check the option due to needing packet_size	*/
      else if(check == 1){
	if((unsigned long)config_setting_get_int64(setting) != opt->packet_size*opt->buf_num_elems)
	  E("Check failed for packetsize");
	return -1;
      }
      else if(write == 1){
	filesize = opt->packet_size*opt->buf_num_elems;
	err = config_setting_set_int64(setting,filesize);
	if(err != CONFIG_TRUE){
	  E("Writing filesize: %d",, err);
	  return -1;
	}
      }
      else{
	filesize_found=1;
	filesize = (unsigned long)config_setting_get_int64(setting);
      }
    }
    /* "Legacy" stuff*/
    CFG_ELIF("buf_elem_size"){
      if(config_setting_type(setting) != CONFIG_TYPE_INT64)
	return -1;
      if(check==1){
	if(((unsigned long)config_setting_get_int64(setting)) != opt->packet_size)
	  return -1;
      }
      else if(write==1){
	err = config_setting_set_int64(setting,opt->packet_size);
	if(err != CONFIG_TRUE){
	  E("Writing packetsize: %d",, err);
	  return -1;
	}
      }
      else{
	opt->packet_size = (unsigned long)config_setting_get_int64(setting);
      }
    }
    /* Used when checking a schedule */
    CFG_ELIF("record"){
      if(config_setting_type(setting) != CONFIG_TYPE_INT){
	E("cfg type not matched");
	return -1;
      }
      if(check==1){
	int hur = config_setting_get_int(setting);
	if(hur && (opt->optbits & READMODE)){
	  E("Check doesn't match");
	  return -1;
	}
	else if (!hur && !(opt->optbits & READMODE)){
	  E("Check doesn't match");
	  return -1;
	}
      }
      else if(write==1){
	int value=0;
	/* record is 1, when readmode is 0 */
	if(!(opt->optbits & READMODE))
	  value=1;
	err = config_setting_set_int(setting, value);
	if(err != CONFIG_TRUE){
	  E("Writing recordmode: %d",, err);
	  return -1;
	}
      }
      else{
	if(config_setting_get_int(setting) == 1)
	  opt->optbits &= ~READMODE;
	else
	  opt->optbits |= READMODE;

      }
    }
    CFG_ELIF("starting_time"){
      if(config_setting_type(setting) != CONFIG_TYPE_INT64){
	E("cfg type not matched");
	return -1;
      }
      /* TODO:  Shouldn't need checking */
      else if(write==1){
	err = config_setting_set_int64(setting, opt->starting_time.tv_sec);
	if(err != CONFIG_TRUE){
	  E("Writing starting_time: %d",, err);
	  return -1;
	}
      }
      else
      {
	opt->starting_time.tv_sec = config_setting_get_int64(setting);
      }
    }
    /*There might be some twisted way of capitalizing the strings	*/
    /* but meh!								*/
    CFG_ELIF("writer"){
      if(config_setting_type(setting) != CONFIG_TYPE_STRING){
	E("writer type not correct");
	return -1;
      }
      if(check==1){
	/* Do nothing! */ 
      }
      else if(write==1){
	switch(opt->optbits & LOCKER_REC){
	  case REC_DEF:
	    err = config_setting_set_string(setting, "def");
	    break;
	  case REC_AIO:
	    err = config_setting_set_string(setting, "aio");
	    break;
	  case REC_WRITEV:
	    err = config_setting_set_string(setting, "writev");
	    break;
	  case REC_SPLICER:
	    err = config_setting_set_string(setting, "splice");
	    break;
	  case REC_DUMMY:
	    err = config_setting_set_string(setting, "dummy");
	    break;
	  default:
	    E("Unknown writer");
	    return -1;
	}
	CHECK_CFG("writer");
      }
      else{
	opt->optbits &= ~LOCKER_REC;
	if (!strcmp(config_setting_get_string(setting), "def")){
	  /*
	     opt->rec_type = REC_DEF;
	     opt->async = 0;
	     */
	  opt->optbits |= REC_DEF;
	  opt->optbits &= ~ASYNC_WRITE;
	}
#ifdef HAVE_LIBAIO
	else if (!strcmp(config_setting_get_string(setting), "aio")){
	  /*
	     opt->rec_type = REC_AIO;
	     opt->async = 1;
	     */
	  opt->optbits |= REC_AIO|ASYNC_WRITE;
	}
#endif
	else if (!strcmp(config_setting_get_string(setting), "splice")){
	  /*
	     opt->rec_type = REC_SPLICER;
	     opt->async = 0;
	     */
	  opt->optbits |= REC_SPLICER;
	  opt->optbits &= ~ASYNC_WRITE;
	  opt->optbits |= CAN_STRIP_BYTES;
	}
	else if (!strcmp(config_setting_get_string(setting), "dummy")){
	  /*
	     opt->rec_type = REC_DUMMY;
	     opt->buf_type = WRITER_DUMMY;
	     */
	  opt->optbits &= ~LOCKER_WRITER;
	  opt->optbits |= REC_DUMMY|WRITER_DUMMY;
	  opt->optbits &= ~ASYNC_WRITE;
	}
	else if (!strcmp(config_setting_get_string(setting), "writev")){
	  /*
	     opt->rec_type = REC_DEF;
	     opt->async = 0;
	     */
	  opt->optbits &= ~LOCKER_WRITER;
	  opt->optbits |= REC_WRITEV;
	  opt->optbits &= ~ASYNC_WRITE;
	  opt->optbits |= CAN_STRIP_BYTES;
	}
	else {
	  LOGERR("Unknown mode type [%s]\n", config_setting_get_string(setting));
	  return -1;
	}
      }
    }
    CFG_ELIF("capture"){
      if(config_setting_type(setting) != CONFIG_TYPE_STRING){
	E("capture type not correct");
	return -1;
      }
      if(check==1){
	/* Do nothing! */ 
      }
      else if(write==1){
	switch(opt->optbits & LOCKER_CAPTURE){
	  case CAPTURE_W_FANOUT:
	    err = config_setting_set_string(setting, "fanout");
	    break;
	  case CAPTURE_W_UDPSTREAM:
	    err = config_setting_set_string(setting, "udpstream");
	    break;
	  case CAPTURE_W_SPLICER:
	    err = config_setting_set_string(setting, "sendfile");
	    break;
	  case CAPTURE_W_DISK2FILE:
	    err = config_setting_set_string(setting, "sendfile");
	    break;
	  default:
	    E("Unknown capture");
	    return -1;
	}
	CHECK_CFG("capture");
      }
      else{
	opt->optbits &= ~LOCKER_CAPTURE;
	if (!strcmp(config_setting_get_string(setting), "fanout")){
	  //opt->capture_type = CAPTURE_W_FANOUT;
	  opt->optbits |= CAPTURE_W_FANOUT;
	}
	else if (!strcmp(config_setting_get_string(setting), "udpstream")){
	  //opt->capture_type = CAPTURE_W_UDPSTREAM;
	  opt->optbits |= CAPTURE_W_UDPSTREAM;
	}
	else if (!strcmp(config_setting_get_string(setting), "sendfile")){
	  //opt->capture_type = CAPTURE_W_SPLICER;
	  opt->optbits |= CAPTURE_W_SPLICER;
	}
	else if (!strcmp(config_setting_get_string(setting), "disk2file")){
	  //opt->capture_type = CAPTURE_W_SPLICER;
	  opt->optbits |= CAPTURE_W_DISK2FILE;
	  opt->optbits |= READMODE;
	}
	else {
	  LOGERR("Unknown packet capture type [%s]\n", config_setting_get_string(setting));
	  return -1;
	}
      }
    }
    CFG_ELIF("datatype"){
      if(config_setting_type(setting) != CONFIG_TYPE_STRING){
	E("datatype type not correct");
	return -1;
      }
      if(check==1){
	/* Do nothing! */ 
      }
      else if(write==1){
	switch(opt->optbits & LOCKER_DATATYPE){
	  case DATATYPE_UNKNOWN:
	    err = config_setting_set_string(setting, "unknown");
	    break;
	  case DATATYPE_VDIF:
	    err = config_setting_set_string(setting, "vdif");
	    break;
	  case DATATYPE_MARK5B:
	    err = config_setting_set_string(setting, "mark5b");
	    break;
	  case DATATYPE_UDPMON:
	    err = config_setting_set_string(setting, "udpmon");
	    break;
	  case DATATYPE_MARK5BNET:
	    err = config_setting_set_string(setting, "mark5bnet");
	    break;
	  default:
	    E("Unknown datatype");
	    return -1;
	}
	CHECK_CFG("datatype");
      }
      else{
	opt->optbits &= ~LOCKER_DATATYPE;
	if (!strcmp(config_setting_get_string(setting), "unknown")){
	  opt->optbits |= DATATYPE_UNKNOWN;
	}
	else if (!strcmp(config_setting_get_string(setting), "vdif")){
	  opt->optbits |= DATATYPE_VDIF;
	}
	else if (!strcmp(config_setting_get_string(setting), "mark5bnet")){
	  opt->optbits |= DATATYPE_MARK5BNET;
	}
	else if (!strcmp(config_setting_get_string(setting), "mark5b")){
	  opt->optbits |= DATATYPE_MARK5B;
	}
	else if (!strcmp(config_setting_get_string(setting), "udpmon")){
	  opt->optbits |= DATATYPE_UDPMON;
	}
	else {
	  LOGERR("Unknown data type [%s]\n", config_setting_get_string(setting));
	  return -1;
	}
      }
    }
    CFG_FULL_BOOLEAN(USE_HUGEPAGE, "use_hugepage")
    //CFG_FULL_BOOLEAN(CHECK_SEQUENCE, "check_sequence")
    CFG_FULL_BOOLEAN(USE_RX_RING, "use_rx_ring")
    CFG_FULL_BOOLEAN(VERBOSE, "verbose")
    CFG_FULL_STR(filename)
    /* Could have done these with concatenation .. */
      CFG_FULL_UINT64((*opt->cumul),"cumul")
      CFG_FULL_STR(device_name)
      CFG_FULL_STR(disk2fileoutput)
      CFG_FULL_UINT64(opt->optbits, "optbits")
      CFG_FULL_UINT64(opt->last_packet, "last_packet")
      CFG_FULL_UINT64(opt->time, "time")
      CFG_FULL_INT(opt->port, "port")
      CFG_FULL_UINT64(opt->minmem, "minmem")
      CFG_FULL_UINT64(opt->maxmem, "maxmem")
      CFG_FULL_INT(opt->n_threads, "n_threads")
      CFG_FULL_INT(opt->n_drives, "n_drives")
      CFG_FULL_INT(opt->offset, "offset")
      CFG_FULL_INT(opt->rate, "rate")
      CFG_FULL_UINT64(opt->do_w_stuff_every, "do_w_stuff_every")
      CFG_FULL_INT(opt->wait_nanoseconds, "wait_nanoseconds")
      CFG_FULL_UINT64(opt->packet_size, "packet_size")
      CFG_FULL_INT(opt->buf_num_elems, "buf_num_elems")
      CFG_FULL_INT(opt->buf_division, "buf_division")
      CFG_FULL_STR(hostname)
      CFG_FULL_UINT64(opt->serverip, "serverip")
      CFG_FULL_UINT64((*opt->total_packets), "total_packets")

      setting = config_setting_get_elem(root,++index);
  }
  /* Only use filesize here, since it needs packet_size */
  if(filesize_found==1){
    opt->buf_num_elems = filesize/opt->packet_size;
    opt->do_w_stuff_every = filesize/((unsigned long)opt->buf_division);
  }
  /*
  if(opt->disk2fileoutput != NULL){
    opt->optbits &= ~LOCKER_CAPTURE;
    opt->optbits |= CAPTURE_W_DISK2FILE;
    opt->optbits |= READMODE;
  }
  */

  return 0;
}
/* Init a rec cfg */
int stub_rec_cfg(config_setting_t *root, struct opt_s *opt){
  int err;
  config_setting_t *setting;
  setting = config_setting_add(root, "packet_size", CONFIG_TYPE_INT64);
  CHECK_ERR_NONNULL(setting, "add packet_size");
  if(opt != NULL){
    if(opt->optbits & CAN_STRIP_BYTES)
      err = config_setting_set_int64(setting, opt->packet_size-opt->offset);
    else
      err = config_setting_set_int64(setting, opt->packet_size);
    CHECK_CFG("set packet size");
  }
  if(opt->optbits & CAN_STRIP_BYTES){
    setting = config_setting_add(root, "offset_onwrite", CONFIG_TYPE_INT);
    CHECK_ERR_NONNULL(setting, "add offset");
    if(opt != NULL){
      err = config_setting_set_int(setting, opt->offset);
      CHECK_CFG("set offset");
    }
  }
  setting = config_setting_add(root, "cumul", CONFIG_TYPE_INT64);
  CHECK_ERR_NONNULL(setting, "add cumul");
  if(opt != NULL){
    err = config_setting_set_int64(setting, *opt->cumul);
    CHECK_CFG("set cumul");
  }
  /* If we're using the simpler buffer calculation, which fixes the 	*/
  /* size of the files, we don't need filesize etc. here anymore	*/
#if !defined(SIMPLE_BUFCACL) && !defined(SIMPLE_BUFCACL_SINGLEFILE)
  setting = config_setting_add(root, "filesize", CONFIG_TYPE_INT64);
  CHECK_ERR_NONNULL(setting, "add filesize");
  if(opt != NULL){
    err = config_setting_set_int64(setting, opt->filesize);
    CHECK_CFG("set filesize");
  }
#endif
  setting = config_setting_add(root, "total_packets", CONFIG_TYPE_INT64);
  CHECK_ERR_NONNULL(setting, "add total_packets");
  if(opt != NULL){
    err = config_setting_set_int64(setting, *opt->total_packets);
    CHECK_CFG("set total packetsize");
  }
#ifndef SIMPLE_BUFCACL
  setting = config_setting_add(root, "buf_division", CONFIG_TYPE_INT);
  CHECK_ERR_NONNULL(setting, "add buf_division");
  if(opt != NULL){
    err = config_setting_set_int64(setting, opt->buf_division);
    CHECK_CFG("set buf_division");
  }
#endif
  return 0;
}
int stub_full_cfg(config_setting_t *root){
  config_setting_t *setting;
  //stub_rec_cfg(root);
  CFG_ADD_INT64(packet_size);
  CFG_ADD_INT64(optbits);
  CFG_ADD_STR(device_name);
  CFG_ADD_INT64(time);
  CFG_ADD_INT(port);
  CFG_ADD_INT64(minmem);
  CFG_ADD_INT64(maxmem);
  CFG_ADD_INT(n_threads);
  CFG_ADD_INT(n_drives);
  CFG_ADD_INT(rate);
  CFG_ADD_INT64(do_w_stuff_every);
  CFG_ADD_INT64(wait_nanoseconds);
  CFG_ADD_STR(hostname);
  CFG_ADD_INT64(serverip);
  return 0;
}
/* Combination of full and session specific conf */
int stub_full_log_cfg(config_setting_t *root){
  stub_rec_cfg(root, NULL);
  stub_full_cfg(root);
  return 0;
}
int read_full_cfg(struct opt_s *opt){
  int err;
  if(opt->cfgfile == NULL){
    E("no cfgfile path");
    return -1;
  }
  err = read_cfg(&(opt->cfg),opt->cfgfile);
  CHECK_ERR("Read cfg");
  return set_from_root(opt,NULL,0,0);
}
/* The full_cfg format is a bit different than the init_cfg format	*/
/* Full cfgs have a root element for a session name to distinguish them	*/
/* from one another.							*/
int write_full_cfg(struct opt_s *opt){
  config_setting_t *root, *setting;
  int err=0;
  D("Initializing CFG");
  config_init(&(opt->cfg));
  //err = config_read_file(&(opt->cfg), opt->cfgfile);
  //CHECK_CFG("Load config");
  root = config_root_setting(&(opt->cfg));
  CHECK_ERR_NONNULL(root, "Get root");

  /* Check if cfg already exists					*/
  setting = config_lookup(&(opt->cfg), opt->filename);
  if(setting != NULL){
    LOG("Configlog for %s already present!", opt->filename);
#ifdef IF_DUPLICATE_CFG_ONLY_UPDATE
    LOG("Only updating configlog");
    err = config_setting_remove(root, opt->filename);
#else
#endif
  }

  /* Since were writing, we should check if a cfg  group with the same 	*/
  /* name already exists						*/
  setting = config_setting_add(root, opt->filename, CONFIG_TYPE_GROUP);
  CALL_AND_CHECK(stub_full_cfg, setting);
  CALL_AND_CHECK(set_from_root, opt,setting,0,1);
  return 0;
}
/* TODO: Move this to the disk init */
int init_cfg(struct opt_s *opt){
  config_setting_t *root;
  int err=0;
  int retval=0;
  int i;
  D("Initializing CFG");

  if(opt->optbits & READMODE){
    int found = 0;
    /* For checking on cfg-file consistency */
    //long long packet_size=0,cumul=0,old_packet_size=0,old_cumul=0;//,n_files=0,old_n_files=0;
    char * path = (char*) malloc(sizeof(char)*FILENAME_MAX);
    CHECK_ERR_NONNULL(path, "Filepath malloc");
    for(i=0;i<opt->n_drives;i++){
      sprintf(path, "%s%s%s", opt->filenames[i],opt->filename ,".cfg");
      if(! config_read_file(&(opt->cfg),path)){
	D("%s:%d - %s",, path, config_error_line(&opt->cfg), config_error_text(&opt->cfg));
      }
      else{
	LOG("Config found on %s\n",path); 
	root = config_root_setting(&(opt->cfg));
	if(found == 0){
	  set_from_root(opt,root,0,0);
	  found = 1;
	  D("Getting opts from first config, cumul is %lu",, *opt->cumul);

	  int j;
	  opt->fi = add_fileindex(opt->filename, *(opt->cumul), FILESTATUS_SENDING);
	  if(opt->fi == NULL){
	    E("File index add");
	    retval = -1;
	  }


	  FILOCK(opt->fi);
	  opt->fi->n_packets = *(opt->total_packets);
	  struct fileholder * fh = opt->fi->files;

	  for(j=0;(unsigned)j<opt->fi->n_files;j++){
	    fh->diskid = -1;
	    fh->status = FH_MISSING;
	    fh++;
	  }
	  FIUNLOCK(opt->fi);
	  //opt->fileholders = fh_orig;
	  /* This might differ from cfg nowadays */
	  opt->buf_num_elems = FILESIZE / opt->packet_size;
	  D("Packet size is %ld so num elems is %d",, opt->packet_size, opt->buf_num_elems);
	  D("opts read from first config");
	}
	else{
	  err = set_from_root(opt,root,1,0);
	  if( err != 0)
	    E("Discrepancy in config at %s",, path);
	  else
	    D("Config at %s conforms to previous",, path);
	}
      }
    }
    if(found == 0){
      E("No config file found! This means no recording with said name found");
      if((opt->fi = get_fileindex(opt->filename, 1)) != NULL)
      {
	D("Live recording exists. Returning OK");
	retval = 0;
      }
      else
	retval= -1;
      //return -1;
    }
    else{
      LOG("Config file reading finished\n");
      //retval =0;
    }
    free(path);
    //config_destroy(&opt->cfg);
  }
  else{
    /* Set the root and other settings we need */
    /* NOTE: Done when closing */
    /*
    root = config_root_setting(&(opt->cfg));
    CHECK_ERR_NONNULL(root, "Get root");
    stub_rec_cfg(root, NULL);
    */
    opt->fi =  add_fileindex(opt->filename, 0, FILESTATUS_RECORDING);
    if(opt->fi == NULL){
      E("opt-fi init"); 
      retval = -1;
    }
    else
      retval = 0;
  }
  D("CFG init done");
  return retval;
}
#endif
