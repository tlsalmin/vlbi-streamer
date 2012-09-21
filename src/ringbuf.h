/*
 * ringbuf.h -- Header file on ringbuffer for vlbi-streamer
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
#ifndef RINGBUF_H
#define RINGBUF_H
#include "streamer.h"
#define SLEEP_ON_IO
#define SIMPLE_RINGBUF
#define WRITE_WHOLE_BUFFER

struct ringbuf{
  /*
  void* pwriter_head;
  void* hdwriter_head;
  void* tail;
  */
  //This might just be easier with uints
  struct opt_s* opt;
  //unsigned int optbits;
#ifdef WRITE_WHOLE_BUFFER
  int ready_to_w;
#endif
  int writer_head;
  int hdwriter_head;
  int tail;
  void* buffer;
  //int elem_size;
  //int num_elems;
  int running;
  //unsigned long do_w_stuff_every;
  int async_writes_submitted;

  //int async;
  //int read;
  //pthread_mutex_t *headlock;
  //pthread_cond_t *iosignal;
  int is_blocked;
  int huge_fd;
};

//Increments pwriter head. Returns negative, if 
//buffer is full
int rbuf_init(struct opt_s *opt, struct buffer_entity *be);
int rbuf_close(struct buffer_entity *be, void * stats);
//Increment head, but not over restraint. If can't go further, return -1
void increment_amount(struct ringbuf * rbuf, int * head, int amount);
int diff_max(int a , int b, int max);
struct iovec * gen_iov(struct ringbuf *rbuf, int * count, void* iovecs);
//inline int get_a_packet(struct ringbuf *rbuf);
void * rbuf_get_buf_to_write(struct buffer_entity *be);
//TODO: Change to configurable calls initialized in the ringbuf struct
int dummy_write(struct ringbuf *rbuf);
inline void dummy_return_from_write(struct ringbuf *rbuf);
int rbuf_aio_write(struct buffer_entity* be, int force);
int rbuf_check_hdevents(struct buffer_entity *be);
int rbuf_wait(struct buffer_entity * be);
int rbuf_init_buf_entity(struct opt_s *opt, struct buffer_entity *be);
//int rbuf_init_dummy(struct opt_s *opt, struct recording_entity *re);

int rbuf_check(struct buffer_entity *be);
//int rbuf_write_index_data(struct buffer_entity* be, void * data, int size);
int write_bytes(struct buffer_entity * re, int head, int *tail, int diff);
void rbuf_init_mutex_n_signal(struct buffer_entity *be, void * mutex, void * signal);
#endif
