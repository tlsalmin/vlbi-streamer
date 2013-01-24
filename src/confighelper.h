/*
 * confighelper.h -- Header file for helper functions on libconfig for vlbi-streamer
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
#ifndef CONFIGHELPER_H
#define CONFIGHELPER_H

#include "streamer.h"
#include "config.h"
#define CHECK_CFG_CUSTOM(x,y) do{if(y == CONFIG_FALSE){E(x);return -1;}else{D(x);}}while(0)
#define CHECK_CFG(x) CHECK_CFG_CUSTOM(x,err)
#define SET_I64(x,y) do{setting = config_lookup(cfg, x); CHECK_ERR_NONNULL(setting,"Set "x); err = config_setting_set_int64(setting,y); CHECK_CFG(x);}while(0) 	
#define GET_I64(x,y) do{setting = config_lookup(cfg, x); CHECK_ERR_NONNULL(setting,"Get "x); y = config_setting_get_int64(setting);D("Got "x" is: %lu",,y);}while(0) 	
#define SET_I(x,y) do{setting = config_lookup(cfg, x); CHECK_ERR_NONNULL(setting,"Set "x); err = config_setting_set_int(setting,y); CHECK_CFG(x);}while(0) 	
#define GET_I(x,y) do{setting = config_lookup(cfg, x); CHECK_ERR_NONNULL(setting,"Get "x); y = config_setting_get_int(setting);D("Got "x" is: %d",,y);}while(0) 	

#define OPT(x) opt->x
#define CFG_ELIF(x) else if(strcmp(config_setting_name(setting), x)==0)
  //#define CFG_GET_STR config_setting_get_string(setting)
  //#define CFG_GET_INT64 config_setting_get_int64(setting)
#define CFG_CHK_STR(x) \
    if(check==1){	\
      if(strcmp(config_setting_get_string(setting),OPT(x)) != 0)\
      return -1;\
    }
#define CFG_WRT_STR(x) \
    else if(write==1){\
      err = config_setting_set_string(setting,OPT(x));\
      CHECK_CFG(#x);\
    }
#define CFG_GET_STR(x) \
    else{\
      const char * temp = config_setting_get_string(setting);\
      if(temp != NULL){\
	if(OPT(x) == NULL)\
	OPT(x) = strdup(temp);\
	else{\
	  D("Overwriting string");\
	  if(strcpy(OPT(x),temp) == NULL)\
	  return -1;\
	}\
      }\
      else\
      return -1;\
    }
#define CFG_CHK_UINT64(x) \
    if(check==1){\
      if(((unsigned long)config_setting_get_int64(setting)) != x)\
      return -1;\
    }
#define CFG_WRT_UINT64(x,y) \
    else if(write==1){\
      err = config_setting_set_int64(setting, x);\
      CHECK_CFG(y);\
    }
#define CFG_GET_UINT64(x) \
    else{\
      x = (unsigned long)config_setting_get_int64(setting);\
    }
#define CFG_GET_INT(x) \
    else{\
      x = config_setting_get_int(setting);\
    }
#define CFG_WRT_INT(x,y) \
    else if(write==1){\
      err = config_setting_set_int(setting, x);\
      CHECK_CFG(y);\
    }
#define CFG_CHK_INT(x) \
    if(check==1){\
      if(config_setting_get_int(setting) != x){\
	E(#x "doesn't check out");\
	return -1;\
      }\
    }
#define CFG_FULL_UINT64(x,y) \
    CFG_ELIF(y){\
      if(config_setting_type(setting) != CONFIG_TYPE_INT64){\
	E(#x" Type not correct");\
	return -1;\
      }\
      CFG_CHK_UINT64(x)\
      CFG_WRT_UINT64(x,y)\
      CFG_GET_UINT64(x)\
    }
#define CFG_CHK_BOOLEAN(x,y) \
    if(check==1){\
      int temp = config_setting_get_int(setting);\
      if((temp == 1 && (opt->optbits & x)) || (temp == 0 && !(opt->optbits & x))){\
	E(#x "doesn't check out");\
	return -1;\
      }\
    }
#define CFG_GET_BOOLEAN(x,y) \
    else{\
      if(config_setting_get_int(setting) == 1)\
      opt->optbits |= x;\
      else\
      opt->optbits &= ~x;\
    }
#define CFG_WRT_BOOLEAN(x,y) \
    else if(write==1){\
      if(opt->optbits & x)\
      err = config_setting_set_int(setting, 1);\
      else\
      err = config_setting_set_int(setting, 0);\
      CHECK_CFG(y);\
    }
#define CFG_FULL_BOOLEAN(x,y) \
    CFG_ELIF(y){\
      if(config_setting_type(setting) != CONFIG_TYPE_INT){\
	E(#x" type not correct");\
	return -1;\
      }\
      D("Found " y " in cfg");\
      CFG_CHK_BOOLEAN(x,y)\
      CFG_WRT_BOOLEAN(x,y)\
      CFG_GET_BOOLEAN(x,y)\
    }
#define CFG_FULL_STR(x) \
    CFG_ELIF(#x){\
      if(config_setting_type(setting) != CONFIG_TYPE_STRING){\
	E(#x" Not string type");\
	return -1;\
      }\
      CFG_CHK_STR(x)\
      CFG_WRT_STR(x)\
      CFG_GET_STR(x)\
    }
#define CFG_FULL_INT(x,y)\
    CFG_ELIF(y){\
      if(config_setting_type(setting) != CONFIG_TYPE_INT)	\
      return -1;\
      CFG_CHK_INT(x)\
      CFG_WRT_INT(x,y)\
      CFG_GET_INT(x)\
    }
#define CFG_ADD_INT64(x)\
    do{\
      setting = config_setting_add(root, #x, CONFIG_TYPE_INT64);\
      CHECK_ERR_NONNULL(setting, "add "#x);\
    }while(0)
#define CFG_ADD_STR(x)\
    do{\
      setting = config_setting_add(root, #x, CONFIG_TYPE_STRING);\
      CHECK_ERR_NONNULL(setting, "add "#x);\
    }while(0)
#define CFG_ADD_INT(x)\
    do{\
      setting = config_setting_add(root, #x, CONFIG_TYPE_INT);\
      CHECK_ERR_NONNULL(setting, "add "#x);\
    }while(0)



int write_cfgs_to_disks(struct opt_s *opt);
int set_from_root(struct opt_s * opt, config_setting_t *root, int check, int write);
int read_full_cfg(struct opt_s *opt);
int write_cfg(config_t *cfg, char* filename);
int write_cfg_for_rec(struct opt_s * opt, char* filename);
int read_cfg(config_t *cfg, char * filename);
int update_cfg(struct opt_s *opt, struct config_t * cfg);
int init_cfg(struct opt_s *opt);
int stub_rec_cfg(config_setting_t *root, struct opt_s *opt);
#endif /* CONFIGHELPER_H */
