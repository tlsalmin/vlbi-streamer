/*
 * timekeeper.c -- Interrupt timer 
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
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <pthread.h>
#include <stdlib.h>

struct listed_entity * root;
pthread_spinlock_t *timelock;

int init_timekeeper(){
  timelock = (pthread_spinlock_t*)malloc(sizeof(pthread_spinlock_t));
  pthread_spin_init(timelock, PTHREAD_PROCESS_SHARED);
  root = NULL;
}
struct interrupt_action* add_action(void (*action), void* param, int mikroseconds){
  (void)action;
  (void)param;
  (void)mikroseconds;
}
int remove_action(struct interrupt_action* ia){
  (void)ia;
}

