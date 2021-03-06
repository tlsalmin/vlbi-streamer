/*
 * udpstream.c -- TCP packet receiver and sender for vlbi-streamer
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
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/mman.h> 	//for MMAP and poll
#include <linux/mman.h> //for MMAP and poll
#include <sys/poll.h>

#include <pthread.h>
#include <assert.h>

#ifdef HAVE_RATELIMITER
#include <time.h> 
#endif
#include <unistd.h>

#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <arpa/inet.h>
#ifdef HAVE_LINUX_NET_TSTAMP_H
#include <linux/net_tstamp.h>
#endif
#include <netinet/in.h>
#include <endian.h>

#include <net/if.h>
#include "udp_stream.h"
#include "resourcetree.h"
#include "timer.h"
//#include "sockethandling.h"

extern FILE* logfile;

/* Using this until solved properly 			*/
/* Most of TPACKET-stuff is stolen from codemonkey blog */
/* http://codemonkeytips.blogspot.com/			*/
/* Offset of data from start of frame			*/
#define PKT_OFFSET      (TPACKET_ALIGN(sizeof(struct tpacket_hdr)) + \
                         TPACKET_ALIGN(sizeof(struct sockaddr_ll)))
#define PLOTTABLE_SEND_DEBUG 0

int udps_bind_rx(struct socketopts * spec_ops){
  struct tpacket_req req;
  int err;

  //TODO: Fix do_w_stuff_every so we can set it here accordingly to block_size. 
  /* I guess I need to just calculate this .. */
  /* FIX: Just set frame size as n-larger so its 16 divisable. */

  unsigned long total_mem_div_blocksize = (spec_ops->opt->packet_size*spec_ops->opt->buf_num_elems*spec_ops->opt->n_threads)/(spec_ops->opt->do_w_stuff_every);
  //req.tp_block_size = spec_ops->opt->packet_size*(spec_ops->opt->buf_num_elems)/4096;
  req.tp_block_size = spec_ops->opt->do_w_stuff_every;
  req.tp_frame_size = spec_ops->opt->packet_size;
  req.tp_frame_nr = spec_ops->opt->buf_num_elems*(spec_ops->opt->n_threads);
  //req.tp_block_nr = spec_ops->opt->n_threads;
  req.tp_block_nr = total_mem_div_blocksize;

  D("Block size: %d B Frame size: %d B Block nr: %d Frame nr: %d Max order:",req.tp_block_size, req.tp_frame_size, req.tp_block_nr, req.tp_frame_nr);

  err = setsockopt(spec_ops->fd, SOL_PACKET, PACKET_RX_RING, (void *) &req, sizeof(req));
  CHECK_ERR("RX_RING SETSOCKOPT");

  //int flags = MAP_ANONYMOUS|MAP_SHARED;
  int flags = MAP_SHARED;
#if(HAVE_HUGEPAGES)
  if(spec_ops->opt->optbits & USE_HUGEPAGE)
    flags |= MAP_HUGETLB;
#endif
  ASSERT((req.tp_block_size*req.tp_block_nr) % getpagesize() == 0);

  long maxmem = sysconf(_SC_AVPHYS_PAGES)*sysconf(_SC_PAGESIZE);
  unsigned long hog_memory =  (unsigned long)req.tp_block_size*((unsigned long)req.tp_block_nr);
  if((long unsigned)maxmem < hog_memory){
    E("Error in mem init. Memory available: %ld Memory wanted:  %lu",maxmem, hog_memory);
    return -1;
  }

  spec_ops->opt->buffer = mmap(0, (unsigned long)req.tp_block_size*((unsigned long)req.tp_block_nr), PROT_READ|PROT_WRITE , flags, spec_ops->fd,0);
  if((long)spec_ops->opt->buffer <= 0){
    perror("Ring MMAP");
    return -1;
  }

  if(spec_ops->opt->address_to_bind_to  !=NULL){
    //spec_ops->sin->sin_family = PF_PACKET;           
    //struct sockaddr_ll ll;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strcpy(ifr.ifr_name, spec_ops->opt->address_to_bind_to);
    err = ioctl(spec_ops->fd, SIOCGIFINDEX, &ifr);
    CHECK_ERR("SIOCGIFINDEX");

  }

  return 0;
}
int setup_udp_socket(struct opt_s * opt, struct streamer_entity *se)
{
  int err;
  struct socketopts *spec_ops =(struct socketopts *) malloc(sizeof(struct socketopts));
  memset(spec_ops, 0, sizeof(struct socketopts));
  CHECK_ERR_NONNULL(spec_ops, "spec ops malloc");
  //spec_ops->running = 1;
  se->opt = (void*)spec_ops;

  spec_ops->opt = opt;

  if(spec_ops->opt->optbits & USE_RX_RING){
    spec_ops->fd = socket(PF_PACKET, SOCK_DGRAM, htons(ETH_P_IP));
    D("Socket initialized with PF_PACKET");
  }
  else
  {
    /* Setting to -1 to check if loop is done at all 	*/
    spec_ops->fd = -1;
    char port[12];
    memset(port, 0,sizeof(char)*12);
    sprintf(port,"%d", spec_ops->opt->port);
    err = create_socket(&(spec_ops->fd), port, &(spec_ops->servinfo), spec_ops->opt->hostname, SOCK_DGRAM, &(spec_ops->p), spec_ops->opt->optbits, spec_ops->opt->address_to_bind_to);
    CHECK_ERR("Create socket");
  }
  if(!(opt->optbits & READMODE) && opt->hostname != NULL){
    char port[12];
    memset(port, 0,sizeof(char)*12);
    sprintf(port,"%d", spec_ops->opt->port);
    err = create_socket(&(spec_ops->fd_send), port, &(spec_ops->servinfo_simusend), spec_ops->opt->hostname, SOCK_DGRAM, &(spec_ops->p_send), spec_ops->opt->optbits, NULL);
    if(err != 0)
      E("Error in creating simusend socket. Not quitting");
  }

  if (spec_ops->fd < 0) {
    perror("socket");
    INIT_ERROR
  }
  err = socket_common_init_stuff(spec_ops->opt, MODE_FROM_OPTS, &(spec_ops->fd));
  CHECK_ERR("Common init");


  if(spec_ops->opt->optbits & USE_RX_RING){
    err = udps_bind_rx(spec_ops);
    CHECK_ERR("BIND RX");
  }
  else{
    /* Moving binding to start of receive/send */
  }

  /* MMap the ring for rx-ring */


  return 0;
}
int udp_sender_sendcmd(struct streamer_entity *se, struct sender_tracking *st)
{
  int err;
  struct socketopts *spec_ops = se->opt;
  err = sendto(spec_ops->fd, se->be->buffer+((*spec_ops->inc)+spec_ops->opt->offset), (spec_ops->opt->packet_size-spec_ops->opt->offset), 0, spec_ops->p->ai_addr,spec_ops->p->ai_addrlen);


  // Increment to the next sendable packet
  if(err < 0){
    perror("Send packet");
    se->close_socket(se);
    return -1;
    //TODO: How to handle error case? Either shut down all threads or keep on trying
    //pthread_exit(NULL);
    //break;
  }
  else if((unsigned)err != spec_ops->opt->packet_size){
    E("Sent only %d, when wanted to send %ld", err, spec_ops->opt->packet_size);
  }
  else{
    st->packets_sent++;
    spec_ops->total_transacted_bytes += err;
    //st->inc+=spec_ops->opt->packet_size;
    (*spec_ops->inc)+=spec_ops->opt->packet_size;
    st->packetcounter--;
  }
  return 0;
}
/*
 * Sending handler for an UDP-stream
 *
 * NOTE: pthreads requires this arguments function to be void* so no  struct streaming_entity
 *
 * This is almost the same as udp_handler, but making it bidirectional might have overcomplicated
 * the logic and lead to high probability of bugs
 *
 */
void * udp_sender(void *streamo)
{
  //struct streamer_entity * se = (struct streamer_entity*) se;
  //struct udpopts *spec_ops = se->opt;
  generic_sendloop((struct streamer_entity*)streamo, 1, udp_sender_sendcmd, bboundary_packetnum);
  pthread_exit(NULL);
}

/* RX-ring doesn't support simultaneous sending 	*/
void* udp_rxring(void *streamo)
{
  int err = 0;
  //long i=0;
  long j = 0;
  //TODO: Get the timeout from main here.
  int timeout = 1000;
  struct streamer_entity *se =(struct streamer_entity*)streamo;
  struct socketopts *spec_ops = (struct socketopts *)se->opt;
  struct tpacket_hdr* hdr = spec_ops->opt->buffer + j*(spec_ops->opt->packet_size); 
  struct pollfd pfd;
  int bufnum = 0;
  unsigned long *inc;

  struct rxring_request rxr;

  spec_ops->total_transacted_bytes = 0;
  spec_ops->opt->total_packets = 0;
  spec_ops->out_of_order = 0;
  spec_ops->incomplete = 0;
  spec_ops->missing = 0;
  D("PKT OFFSET %lu tpacket_offset %lu", PKT_OFFSET, sizeof(struct tpacket_hdr));

  pfd.fd = spec_ops->fd;
  pfd.revents = 0;
  pfd.events = POLLIN|POLLRDNORM|POLLERR;
  D("Starting mmap polling");

  rxr.id = &(spec_ops->opt->cumul);
  rxr.bufnum = &bufnum;
  se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch, spec_ops->opt,(void*)&rxr, NULL,1);
  //inc = se->be->get_inc(se->be);
  se->be->simple_get_writebuf(se->be, &inc);
  CHECK_AND_EXIT(se->be);

  while(get_status_from_opt(spec_ops->opt) == STATUS_RUNNING){
    if(!(hdr->tp_status  & TP_STATUS_USER)){
      //D("Polling pfd");
      err = poll(&pfd, 1, timeout);
      SILENT_CHECK_ERRP_LTZ("Polled");
    }
    while(hdr->tp_status & TP_STATUS_USER){
      j++;

      if(hdr->tp_status & TP_STATUS_COPY){
	spec_ops->incomplete++;
	spec_ops->opt->total_packets++;
      }
      else if (hdr ->tp_status & TP_STATUS_LOSING){
	spec_ops->missing++;
	spec_ops->opt->total_packets++;
      }
      else{
	spec_ops->total_transacted_bytes += hdr->tp_len;
	spec_ops->opt->total_packets++;
	//(*inc)++;
	//TODO: Should we add s the extra?
	(*inc)+=hdr->tp_len;
      }

      /* A buffer is ready for writing */
      if((j % spec_ops->opt->buf_num_elems) == 0){
	D("Buffo!");

	se->be->set_ready_and_signal(se->be,0);
	/* Increment file counter! */
	//spec_ops->opt->n_files++;

	se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch, spec_ops->opt,&rxr, NULL,1);
	CHECK_AND_EXIT(se->be);
	//inc = se->be->get_inc(se->be);
	se->be->simple_get_writebuf(se->be, &inc);

	if(j==spec_ops->opt->buf_num_elems*(spec_ops->opt->n_threads)){
	  j=0;
	  bufnum =0;
	}
	else
	  bufnum++;

	/* cumul is tracking the n of files we've received */
	spec_ops->opt->cumul++;
      }
#ifdef SHOW_PACKET_METADATA
      D("Metadata for %ld packet: status: %lu, len: %u, snaplen: %u, MAC: %hd, net: %hd, sec %u, usec: %u\n", j, hdr->tp_status, hdr->tp_len, hdr->tp_snaplen, hdr->tp_mac, hdr->tp_net, hdr->tp_sec, hdr->tp_usec);
#endif

      hdr->tp_status = TP_STATUS_KERNEL;
      hdr = spec_ops->opt->buffer + j*(spec_ops->opt->packet_size); 
    }
    //D("Packets handled");
    //fprintf(stdout, "i: %d, j: %d\n", i,j);
    hdr = spec_ops->opt->buffer + j*(spec_ops->opt->packet_size); 
  }
  if(j > 0){
    spec_ops->opt->cumul++;
    /* Since n_files starts from 0, we need to increment it here */
    /* What? legacy stuff i presume */
    spec_ops->opt->cumul++;
  }
  D("Saved %lu files", spec_ops->opt->cumul);
  D("Exiting mmap polling");
  //spec_ops->running = 0;
  //spec_ops->opt->status = STATUS_STOPPED;

  pthread_exit(NULL);
}
int jump_to_next_buf(struct streamer_entity* se, struct resq_info* resq){
  D("Jumping to next buffer!");
  struct socketopts* spec_ops = (struct socketopts*)se->opt;
  spec_ops->opt->cumul++;
  /* Check if the buffer before still hasn't	*/
  /* gotten all packets				*/
  if(resq->before != NULL){
    D("Previous file still doesn't have all packets. Writing to disk and setting old packets as missing");
    //spec_ops->missing += spec_ops->opt->buf_num_elems - (*(resq->inc_before));
    /* Now heres a problem. We have to write the whole thing since	*/
    /* we don't know whats missing at this point. TODO: Fix when doing	*/
    /* fillpattern f4lz							*/
    spec_ops->missing += (CALC_BUFSIZE_FROM_OPT(spec_ops->opt)-(*(resq->inc_before)))/spec_ops->opt->packet_size;
    //spec_ops->missing += 
    /* Write the to disk anyhow, so last packets aren't missed	*/
    *(resq->inc_before) = CALC_BUFSIZE_FROM_OPT(spec_ops->opt);
    free_the_buf(resq->before);
    resq->bufstart_before = NULL;
    resq->before = NULL;
    resq->inc_before = NULL;
  }
  /* Check if we have all the packets for this file */
  if((unsigned long)*(resq->inc) == (spec_ops->opt->buf_num_elems*(spec_ops->opt->packet_size)))
  {
    D("All packets for current file received OK. rsqinc: %ld, needed: %lu", *resq->inc, spec_ops->opt->buf_num_elems*spec_ops->opt->packet_size);
#ifdef FORCE_WRITE_TO_FILESIZE
    *(resq->inc) = CALC_BUFSIZE_FROM_OPT(spec_ops->opt);
#endif
    free_the_buf(se->be);
    se->be = NULL;
    /* First buffer so *before is null 	*/
    resq->bufstart_before = NULL;
    resq->before = NULL;
  }
  /* If not, then leave the old buf hanging 	*/
  else{
    D("Packets missing for current file. Leaving it to hang");
    resq->before = se->be;
    resq->bufstart_before = resq->bufstart;
    resq->inc_before = resq->inc;
  }
  resq->i=0;
  se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch, spec_ops->opt,&(spec_ops->opt->cumul), NULL,1);
  CHECK_AND_EXIT(se->be);
  resq->buf = se->be->simple_get_writebuf(se->be, &resq->inc);
  resq->bufstart = resq->buf;
  /* Set the next seqstart to += buf_num_elems		*/
  /* This way we can keep the buffers consistent	*/
  /* without holes from the resequencing logic		*/
  resq->seqstart_current += spec_ops->opt->buf_num_elems;

  return 0;
}
void*  calc_bufpos_general(void* header, struct streamer_entity* se, struct resq_info *resq){
  /* The seqnum is the first 64bits of the packet 	*/
  struct socketopts* spec_ops = (struct socketopts*)se->opt;

  long seqnum;
  switch (spec_ops->opt->optbits & LOCKER_DATATYPE){
    case DATATYPE_UDPMON:
      seqnum = getseq_udpmon(header);
      break;
    case DATATYPE_VDIF:
      seqnum = getseq_vdif(header, resq);
      break;
    case DATATYPE_MARK5BNET:
      seqnum = getseq_mark5b_net(header);
      break;
    case DATATYPE_MARK5B:
      //seqnum = getseq_mark5b(header);
      //TODO!
      /* Used is in trouble if hes sending plain mark5bs anyway */
      seqnum = resq->current_seq+1;
      break;
    default:
      E("Invalid datatype set!");
      return NULL;
  }

  if (resq->current_seq == INT64_MAX && resq->seqstart_current== INT64_MAX){
    resq->current_seq = resq->seqstart_current = seqnum;
    //(*(resq->inc))++;
    (*(resq->inc))+=spec_ops->opt->packet_size;
    resq->i++;
    resq->buf+= spec_ops->opt->packet_size;
    D("Got first packet with seqnum %ld", seqnum);
    return header;
  }

  /* Preliminary case					*/
  /* Packet in order					*/
  if(seqnum == resq->current_seq+1){
    resq->current_seq++;

    //(*(resq->inc))++;
    (*(resq->inc))+=spec_ops->opt->packet_size;
    resq->i++;
    resq->buf+= spec_ops->opt->packet_size;

    return header;
  }
  else if(seqnum == resq->current_seq){
    D("Packet with same seqnum %ld! Dropping..", seqnum);
    return NULL;
  }
  else{
    long diff_from_start = seqnum - resq->seqstart_current;
    long diff_to_current = seqnum - (resq->current_seq);
    D("Current status: i: %d, cumul: %lu, current_seq %ld, seqnum: %ld inc: %ld,  diff_from_start %ld, diff_from_current %ld seqstart %ld", resq->i, spec_ops->opt->cumul, resq->current_seq, seqnum, *resq->inc,  diff_from_start, diff_to_current, resq->seqstart_current);
    if (diff_to_current < 0){
      D("Delayed packet. Returning correct pos. Seqnum: %ld old seqnum: %ld", seqnum, resq->current_seq);
      if(diff_from_start < 0){
	D("Delayed beyond file. Writing to buffer_before");

	if(resq->bufstart_before == NULL){
	  E("Packet before first packet! Dropping!");
	  spec_ops->missing++;
	  return NULL;
	}
	else if (diff_from_start < -((long)spec_ops->opt->buf_num_elems)){
	  D("Packet beyond previous buffer. Dropping");
	  spec_ops->missing++;
	  return NULL;
	}

	ASSERT(resq->inc_before != NULL);

	//(*(resq->inc_before))++;
	(*(resq->inc_before))+= spec_ops->opt->packet_size;

	// Copy to the old pos
	resq->usebuf = resq->bufstart_before + (spec_ops->opt->buf_num_elems + diff_from_start)*((long)spec_ops->opt->packet_size);
	memcpy(resq->usebuf, resq->buf, spec_ops->opt->packet_size);

	//if(*(resq->inc_before) == spec_ops->opt->buf_num_elems){
	if(*(resq->inc_before) + spec_ops->opt->packet_size > CALC_BUFSIZE_FROM_OPT(spec_ops->opt))
	{
	  D("Buffer before is ready. Freeing it");
	  free_the_buf(resq->before);
	  resq->bufstart_before = NULL;
	  resq->before = NULL;
	  resq->inc_before = NULL;
	}
	return NULL;
      }
      //SOMsdf
	else
	{
	  D("Packet behind order, but inside this buffer.");
	  (*(resq->inc))+=spec_ops->opt->packet_size;

	  resq->usebuf = (resq->bufstart + ((diff_from_start)*spec_ops->opt->packet_size));
	  memcpy(resq->usebuf, resq->buf, spec_ops->opt->packet_size);

	  return NULL;
	}
      }
      // seqnum > current
      else
      {
	D("Seqnum larger than current: %ld, old seqnum: %ld", seqnum, resq->current_seq);
	if (diff_from_start >= spec_ops->opt->buf_num_elems){
	  D("Packet ahead of time beyond this buffer!");
	  if(diff_from_start >= spec_ops->opt->buf_num_elems*2){
	    D("Packet is way beyond buffer after. Dropping!");
	    spec_ops->missing++;
	    return NULL;
	  }
	  /* Need to jump to next buffer in this case */

	  long i_from_next_seqstart = diff_from_start - (long)spec_ops->opt->buf_num_elems;
	  /* Save pointer to packet		*/
	  void* origbuf = resq->buf;

	  int err = jump_to_next_buf(se, resq);
	  if(err != 0){
	    E("Jump to next failed");
	    return NULL;
	  }

	  resq->usebuf = resq->bufstart + (i_from_next_seqstart)*spec_ops->opt->packet_size;
	  memcpy(resq->usebuf, origbuf, spec_ops->opt->packet_size);

	  //(*(resq->inc))++;
	  (*(resq->inc))+=spec_ops->opt->packet_size;

	  /* Move diff up as thought we'd be receiving normally at the new position 	*/
	  resq->current_seq = seqnum;
	  /* Since i_from_next_seqstart counts also 0 , we need to add 1 here		*/
	  resq->i+=i_from_next_seqstart+1;
	  resq->buf = resq->usebuf + spec_ops->opt->packet_size;

	  return NULL;
	}
	else{
	  D("Packet ahead of time but in this buffer!");
	  //(*(resq->inc))++;
	  (*(resq->inc))+=spec_ops->opt->packet_size;
	  /* Jump to this position. This way we dont have to keep a bitmap of what we have etc.  */

	  //resq->usebuf = resq->bufstart + (diff_from_start)*spec_ops->opt->packet_size;
	  resq->usebuf = resq->bufstart + (((unsigned long)diff_from_start)*(spec_ops->opt->packet_size));
	  memcpy(resq->usebuf, resq->buf, spec_ops->opt->packet_size);
	  //void * temp = memcpy(resq->usebuf, resq->buf, spec_ops->opt->packet_size);

	  /*
	     D("memcpy copied stuff to %lu from start, when diff was %ld", (long unsigned)((temp-resq->bufstart)/spec_ops->opt->packet_size), diff_from_start);
	     D("Indabuff %lu", be64toh(*((unsigned long*)temp)));
	     D("Indabuff usebuf %lu", be64toh(*((unsigned long*)resq->usebuf)));
	     D("Indabuff shoulda %lu", be64toh(*((unsigned long*)resq->bufstart + (((unsigned long)diff_from_start)*spec_ops->opt->packet_size))));

*/

	  /* Since diff_to_current is current_seqnum - seqnum and current_seqnum is for	*/
	  /* the packet received before this, we need to add one here. 			*/
	  resq->i+= diff_to_current;
	  resq->current_seq = seqnum;
	  resq->buf = resq->usebuf + spec_ops->opt->packet_size;

	  return NULL;
	}
      }
    }
    return NULL;
  }
  int udps_handle_received_packet(struct streamer_entity* se, struct resq_info * resq, int received)
  {
    int err;
    struct socketopts* spec_ops = (struct socketopts*)se->opt;
    if(received == 0)
    {
      LOG("UDP_STREAMER: Main thread has shutdown socket\n");
      return 1;
    }
    else if(received < 0){
      if(received == EINTR){
	LOG("UDP_STREAMER: Main thread has shutdown socket\n");
	return 1;
      }
      else{
	perror("RECV error");
	E("Buf start: %lu, end: %lu", (long unsigned)resq->buf, (long unsigned)(resq->buf+spec_ops->opt->packet_size*spec_ops->opt->buf_num_elems));
	fprintf(stderr, "UDP_STREAMER: Buf was at %lu\n", (long unsigned)resq->buf);
	if(!(spec_ops->opt->optbits & DATATYPE_UNKNOWN)){
	  E("Current status: i: %d, cumul: %lu, current_seq %ld,  inc: %ld,   seqstart %ld", resq->i, spec_ops->opt->cumul, resq->current_seq,  *resq->inc,  resq->seqstart_current);
	}
      }
      //spec_ops->running = 0;
      se->stop(se);
      set_status_for_opt(spec_ops->opt, STATUS_ERROR);
      return -1;
    }
    else if((long unsigned)received != spec_ops->opt->packet_size){
      if(get_status_from_opt(spec_ops->opt) == STATUS_RUNNING){
	E("Received packet of size %d, when expected %lu", received, spec_ops->opt->packet_size);
	spec_ops->incomplete++;
	spec_ops->wrongsizeerrors++;
	if(spec_ops->wrongsizeerrors > WRONGSIZELIMITBEFOREEXIT){
	  E("Too many wrong size packets received. Please adjust packet size correctly. Exiting");
	  se->stop(se);
	  set_status_for_opt(spec_ops->opt,STATUS_ERROR);
	  return -1;
	}
      }
    }
    /* Success! */
    //else if(get_status_from_opt(spec_ops->opt) == STATUS_RUNNING){
    else{
      if(spec_ops->opt->hostname != NULL){
	int senderr = sendto(spec_ops->fd_send, resq->buf, spec_ops->opt->packet_size, 0, spec_ops->p_send->ai_addr,spec_ops->p_send->ai_addrlen);
	if(senderr <0 ){
	  perror("send error");
	  E("Send er");
	}
	else if((unsigned long)senderr != spec_ops->opt->packet_size)
	  E("Different size sent onward. NOT HANDLED");
      }
      ASSERT((unsigned)resq->i < spec_ops->opt->buf_num_elems);
      /* i has to keep on running, so we always change	*/
      /* the buffer at a correct spot			*/

      /* Check if we have a func for checking the	*/
      /* correct sequence from the header		*/
      if(!(spec_ops->opt->optbits & DATATYPE_UNKNOWN)){

	if(spec_ops->opt->optbits & WAIT_START_ON_METADATA)
	{
	  int temperr=0;
	  err = get_sec_dif_from_buf(resq->buf, &(resq->tm_s), spec_ops->opt,&temperr);
	  if(temperr == NONEVEN_PACKET){
	    //D("Noneven packet");
	    return 0;
	  }
	  else if(temperr != 0){
	    E("Error in getting metadata");
	    return -1;
	  }
	  else if(err > 0)
	  {
	    //D("Still waiting on start");
	    return 0;
	  }
	  else
	  {
	    LOG("Got first packet in correct metadata second. Starting recording! diff was %d seconds\n", err);
	    spec_ops->opt->optbits &= ~WAIT_START_ON_METADATA;
	    D("Updating our start time according to metadata");
	    TIMERTYPE temptime;
	    GETTIME(temptime);
	    GETSECONDS(spec_ops->opt->starting_time) = GETSECONDS(temptime);
	  }
	}

	/* Calc the position we should have		*/
	if(spec_ops->opt->first_packet == NULL)
	{
	  err = init_header(&(spec_ops->opt->first_packet), spec_ops->opt);
	  if (err != 0)
	  {
	    E("First metadata malloc failed!");
	  }
	  else{
	    err = copy_metadata(spec_ops->opt->first_packet, resq->buf, spec_ops->opt->optbits);
	    if(err != 0)
	    {
	      E("First metadata copying failed!");
	    }
	    spec_ops->opt->resqut = resq;
	  }
	}
	calc_bufpos_general(resq->buf, se, resq);
      }
      else{
	resq->buf+=spec_ops->opt->packet_size;
	(*(resq->inc))+=spec_ops->opt->packet_size;
	resq->i++;
      }
      ASSERT((unsigned)*resq->inc <= CALC_BUFSIZE_FROM_OPT_NOOFFSET(spec_ops->opt));
      spec_ops->total_transacted_bytes +=(unsigned int) received;
      spec_ops->opt->total_packets++;
      if(spec_ops->opt->last_packet == spec_ops->opt->total_packets){
	LOG("Captured %lu packets as specced. Exiting\n", spec_ops->opt->last_packet);
	set_status_for_opt(spec_ops->opt, STATUS_FINISHED);
      }
    }
    return 0;
  }
  int handle_buffer_switch(struct streamer_entity *se , struct resq_info *resq)
  {
    int err;
    struct socketopts* spec_ops = (struct socketopts*)se->opt;
    if((unsigned)resq->i == spec_ops->opt->buf_num_elems)
    {
      D("Buffer filled, Getting another for %s", spec_ops->opt->filename);
      ASSERT(spec_ops->opt->fi != NULL);
      unsigned long n_now = afi_add_to_packets(spec_ops->opt->fi, spec_ops->opt->buf_num_elems);
      D("%s : N packets is now %lu",spec_ops->opt->filename, n_now);

      if(!(spec_ops->opt->optbits & DATATYPE_UNKNOWN)){
	D("Jumping to next buffer normally");
	err = jump_to_next_buf(se, resq);
	if(err < 0){
	  E("Error in jump to next");
	  //spec_ops->running = 0;
	  set_status_for_opt(spec_ops->opt,STATUS_ERROR);
	  return -1;
	}
      }
      else{
	D("Datatype unknown!");
	resq->i=0;
	spec_ops->opt->cumul++;
#ifdef FORCE_WRITE_TO_FILESIZE
	(*resq->inc) = CALC_BUFSIZE_FROM_OPT(spec_ops->opt);
#endif

	D("%s Freeing used buffer to write %lu bytes for file %lu", spec_ops->opt->filename, *(resq->inc), spec_ops->opt->cumul-1);
	free_the_buf(se->be);
	se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch,spec_ops->opt ,&(spec_ops->opt->cumul), NULL,1);
	CHECK_AND_EXIT(se->be);
	D("Got new free for %s. Grabbing buffer", spec_ops->opt->filename);
	resq->buf = se->be->simple_get_writebuf(se->be, &resq->inc);
      }
    }
    return 0;
  }
  int force_reacquire(struct socketopts *spec_ops)
  {
    int err =1;
    //TODO: Is this even sensible?
    TIMERTYPE temptimer;
    GETTIME(temptimer);
    while(GETSECONDS(temptimer) < (GETSECONDS(spec_ops->opt->starting_time) + (long)spec_ops->opt->time) && err != 0)
    {
      sleep(1);
      GETTIME(temptimer);
      /*
	 err = bind_port(spec_ops->servinfo, spec_ops->fd,(spec_ops->opt->optbits & READMODE), (spec_ops->opt->optbits & CONNECT_BEFORE_SENDING));
	 */
    }
    if(err != 0){
      E("Still couldn't get the port. Exiting");
      return -1;
    }
    return 0;
  }
  /*
   * Receiver for UDP-data
   */
  void* udp_receiver(void *streamo)
  {
    int err = 0;

    struct resq_info* resq = (struct resq_info*)malloc(sizeof(struct resq_info));
    memset(resq, 0, sizeof(struct resq_info));

    struct streamer_entity *se =(struct streamer_entity*)streamo;
    struct socketopts *spec_ops = (struct socketopts *)se->opt;


    reset_udpopts_stats(spec_ops);

    LOG("Starting stream capture\n");
    /*
       err = bind_port(spec_ops->servinfo, spec_ops->fd,(spec_ops->opt->optbits & READMODE), (spec_ops->opt->optbits & CONNECT_BEFORE_SENDING));
       if(err != 0){
       E("Error in port binding");
       if(spec_ops->opt->optbits & FORCE_SOCKET_REACQUIRE)
       {
       LOG("Force acquiring\n");
       err = force_reacquire(spec_ops);
       if(err != 0){
       E("Force reacquire failed");
       spec_ops->opt->status = STATUS_ERROR;
       pthread_exit(NULL);
       }
       }
       else{
       spec_ops->opt->status = STATUS_ERROR;
       pthread_exit(NULL);
       }
       }
       */

    se->be = (struct buffer_entity*)get_free(spec_ops->opt->membranch, spec_ops->opt,&(spec_ops->opt->cumul), NULL,1);
    CHECK_AND_EXIT(se->be);

    resq->buf = se->be->simple_get_writebuf(se->be, &resq->inc);

    /* If we have packet resequencing	*/
    if(!(spec_ops->opt->optbits & DATATYPE_UNKNOWN)){
      init_resq(resq);
      if(spec_ops->opt->optbits & WAIT_START_ON_METADATA){
	gmtime_r(&GETSECONDS(spec_ops->opt->starting_time), &(resq->tm_s));
      }
    }



    //while(get_status_from_opt(spec_ops->opt) == STATUS_RUNNING){
    D("Entering receive loop for %s", spec_ops->opt->filename);
    while(get_status_from_opt(spec_ops->opt) & STATUS_RUNNING){
      err = handle_buffer_switch(se,resq);
      if(err != 0){
	LOG("Done or error!");
	set_status_for_opt(spec_ops->opt,STATUS_ERROR);
	break;
      }

      err = recv(spec_ops->fd, resq->buf, spec_ops->opt->packet_size,0);

      err = udps_handle_received_packet(se, resq, err);
      if(err !=0){
	if(err == 1){
	  D("Normal exit");
	  break;
	}
	E("Error in packet receive. Stopping loop!");
	set_status_for_opt(spec_ops->opt,STATUS_ERROR);
	break;
      }
    }
    LOG("UDP_STREAMER: Closing streamer thread\n");
    /* Release last used buffer */
    if(resq->before != NULL){
      *(resq->inc_before) = CALC_BUFSIZE_FROM_OPT(spec_ops->opt);
      free_the_buf(resq->before);
    }
    if(*(resq->inc) == 0){
      se->be->cancel_writebuf(se->be);
      se->be = NULL;
    }
    else{
      if(spec_ops->opt->fi != NULL){
	unsigned long n_now = afi_add_to_packets(spec_ops->opt->fi, resq->i);
	D("N packets is now %lu and received nu, %lu", n_now, spec_ops->opt->total_packets);
      }
      spec_ops->opt->cumul++;
      se->be->set_ready_and_signal(se->be,0);
    }
    /* Set total captured packets as saveable. This should be changed to just */
    /* Use opts total packets anyway.. */
    D("Saved %lu files and %lu packets", spec_ops->opt->cumul, spec_ops->opt->total_packets);

    /* Main thread will free if we have a real datatype */
    if(spec_ops->opt->optbits & DATATYPE_UNKNOWN)
      free(resq);

    /* The default behaviour is STATUS_STOPPED for clean exit and  	*/
    /* STATUS_ERROR for unclean. STATUS_STOPPED is set by stop		*/
    /* And it also shutdowns the socket so we can unblock this thread	*/

    if(get_status_from_opt(spec_ops->opt) != STATUS_STOPPED){
      D("Seems we weren't shut down nicely. Doing close_socket");
      se->close_socket(se);
    }

    pthread_exit(NULL);
  }
  void get_udp_stats(void *sp, void *stats){
    struct stats *stat = (struct stats * ) stats;
    struct socketopts *spec_ops = (struct socketopts*)sp;
    //if(spec_ops->opt->optbits & USE_RX_RING)
    stat->total_packets += spec_ops->opt->total_packets;
    stat->total_bytes += spec_ops->total_transacted_bytes;
    stat->incomplete += spec_ops->incomplete;
    stat->dropped += spec_ops->missing;
    if(spec_ops->opt->last_packet > 0){
      stat->progress = (spec_ops->opt->total_packets*100)/(spec_ops->opt->last_packet);
    }
    else
      stat->progress = -1;
    //stat->files_exchanged = udps_get_fileprogress(spec_ops);
  }
#ifdef CHECK_FOR_BLOCK_BEFORE_SIGNAL
  int udps_is_blocked(struct streamer_entity *se){
    return ((struct socketopts *)(se->opt))->is_blocked;
  }
#endif
  /*
     unsigned long udps_get_max_packets(struct streamer_entity *se){
     return ((struct opts*)(se->opt))->max_num_packets;
     }
     */
  void udps_init_default(struct opt_s *opt, struct streamer_entity *se)
  {
    (void)opt;
    se->init = setup_udp_socket;
    se->close = close_streamer_opts;
    se->get_stats = get_udp_stats;
    se->close_socket = close_socket;
    se->stop = stop_streamer;
    //se->get_max_packets = udps_get_max_packets;
  }

  int udps_init_udp_receiver( struct opt_s *opt, struct streamer_entity *se)
  {

    udps_init_default(opt,se);
    if(opt->optbits & USE_RX_RING)
      se->start = udp_rxring;
    else
      se->start = udp_receiver;

    return se->init(opt, se);
  }

  int udps_init_udp_sender( struct opt_s *opt, struct streamer_entity *se)
  {

    udps_init_default(opt,se);
    se->start = udp_sender;
    return se->init(opt, se);

  }
