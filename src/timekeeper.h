/*
 * timekeeper.h -- Header file for timekeeper.c
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
#ifndef TIMEKEEPER_H
#define TIMEKEEPER_H
#include <sys/time.h>

struct interrupt_action{
  //void (*action)(void*);
  void * param;
  struct itimerval timer;
};

int init_timekeeper();
struct listed_entity* add_action(void (*action), void* param, int mikroseconds);
int remove_action(struct listed_entity* ia);
#endif
