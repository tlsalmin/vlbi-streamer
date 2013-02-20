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
#include "configcommon.h"
#include "config.h"
#define OPT(x) opt->x
  //#define CFG_GET_STR config_setting_get_string(setting)
  //#define CFG_GET_INT64 config_setting_get_int64(setting)


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
