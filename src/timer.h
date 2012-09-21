/*
 * timer.h -- Header file for timer functions
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
#ifndef TIMER_H
#define TIMER_H
#include <unistd.h>
#ifdef TIMERTYPE_GETTIMEOFDAY
#define TIMERTYPE struct timeval 
#define GETTIME(x) gettimeofday(&x,NULL)
  //#define ZEROTIME(x) x.tv_sec =0;x.tv_usec=0;
#define SLEEP_NANOS(x) usleep((x.tv_usec))
#define COPYTIME(from,to) to.tv_sec = from.tv_sec;to.tv_usec=from.tv_usec
#define SETNANOS(x,y) x.tv_usec = (y)/1000
#define SETONE(x) x.tv_usec=1
#define GETNANOS(x) (x).tv_usec*1000
#else
#define TIMERTYPE struct timespec
#define GETTIME(x) clock_gettime(CLOCK_REALTIME, &x)
  //#define ZEROTIME(x) x.tv_sec =0;x.tv_nsec=0;
#define SLEEP_NANOS(x) nanosleep(&x,NULL)
#define COPYTIME(from,to) to.tv_sec = from.tv_sec;to.tv_nsec=from.tv_nsec
#define SETNANOS(x,y) x.tv_nsec = (y)
#define GETNANOS(x) (x).tv_nsec
#define SETONE(x) x.tv_nsec=1
#endif
#define ZEROTIME(x) memset((void*)(&x),0,sizeof(TIMERTYPE))
#include "streamer.h"

long nanodiff(TIMERTYPE * start, TIMERTYPE *end);
void nanoadd(TIMERTYPE * datime, unsigned long nanos_to_add);
void zeroandadd(TIMERTYPE *datime, unsigned long nanos_to_add);
//void specadd(struct timespec * to, struct timespec *from);
int get_sec_diff(TIMERTYPE *timenow, TIMERTYPE* event);
unsigned long get_min_sleeptime();
#endif /* TIMER_H */
