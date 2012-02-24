//Create  THREADS threads to receive fanouts
#define THREADED
//#define OUTPUT
#define MMAP_TECH

#ifdef MMAP_TECH
//volatile struct tpacket_hdr * ps_header_start;
#define RING_BLOCKSIZE 65536
#define RING_FRAME_SIZE 8192
#define RING_BLOCK_NR 1024
//Note that somewhere around 5650 bytes, the ksoftirqd starts
//hogging cpu, but after that it disappears.
/*
#define RING_BLOCKSIZE 4096
#define RING_BLOCK_NR 4
#define RING_FRAME_SIZE 2048
*/

#define RING_FRAME_NR  RING_BLOCKSIZE / RING_FRAME_SIZE * RING_BLOCK_NR
#endif

#ifndef PACKET_FANOUT
#define PACKET_FANOUT		18
#define PACKET_FANOUT_HASH		0
#define PACKET_FANOUT_LB		1
#endif
#ifndef THREADED
#define THREADS 1
#else
#define THREADS 6
#endif
#define PACKET_NUM 100000
#define BUFSIZE 65536
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <time.h>
#ifdef MMAP_TECH
#include <sys/mman.h>
#include <sys/poll.h>
#endif

#include <unistd.h>

#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <arpa/inet.h>

#include <net/if.h>
#include "fanout.h"

static const char *device_name;
static int fanout_type;
static int fanout_id;


//This is a pretty silly way to do this
static int setup_socket(void)
{
  int err, fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_IP));
  //int err, fd = socket(AF_PACKET, SOCK_RAW, 16);
  //int err, fd = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_IP));
  struct sockaddr_ll ll;
  struct ifreq ifr;
  int fanout_arg;
  //int o_rcvbuf;

  if (fd < 0) {
    perror("socket");
    return EXIT_FAILURE;
  }

  memset(&ifr, 0, sizeof(ifr));
  strcpy(ifr.ifr_name, device_name);
  err = ioctl(fd, SIOCGIFINDEX, &ifr);
  if (err < 0) {
    perror("SIOCGIFINDEX");
    return EXIT_FAILURE;
  }

  memset(&ll, 0, sizeof(ll));
  ll.sll_family = AF_PACKET;
  //ll.sll_protocol = ETH_P_ALL;
  ll.sll_ifindex = ifr.ifr_ifindex;
  err = bind(fd, (struct sockaddr *) &ll, sizeof(ll));
  if (err < 0) {
    perror("bind");
    return EXIT_FAILURE;
  }

#ifdef THREADED
  fanout_arg = (fanout_id | (fanout_type << 16));
  err = setsockopt(fd, SOL_PACKET, PACKET_FANOUT,
      &fanout_arg, sizeof(fanout_arg));
  if (err) {
    perror("setsockopt");
    return EXIT_FAILURE;
  }
#endif //THREADED
  return fd;
}
  /*
     o_rcvbuf = 131071;
     err = setsockopt(fd, SOL_PACKET, SO_RCVBUF, &o_rcvbuf, sizeof(o_rcvbuf));
     if (err) {
     perror("RCVBUF augment failed");
     return EXIT_FAILURE;
     }
     */


static void fanout_thread(void)
{
  int fd = setup_socket();
  int limit = PACKET_NUM;
  clock_t t_start;
  double t_total = 0;
  int clock_started = 0;

#ifdef MMAP_TECH
  uint64_t total_captured = 0;
  int err;
  struct tpacket_hdr * ps_header_start;
  /*
  req.tp_block_size= 4096;
    req.tp_frame_size= 2048;
    req.tp_block_nr  = 4;
    req.tp_frame_nr  = 8;
  //Set a ringbuffer for mmap
  */
  //for MMAP_TECH-buffer
  struct tpacket_req req;

  req.tp_block_size = RING_BLOCKSIZE;
  req.tp_frame_size = RING_FRAME_SIZE;
  req.tp_block_nr = RING_BLOCK_NR;
  req.tp_frame_nr = RING_FRAME_NR;
  err = setsockopt(fd, SOL_PACKET, PACKET_RX_RING, (void*) &req, sizeof(req));
  if (err) {
    perror("PACKET_RX_RING failed");
    //return EXIT_FAILURE;
  }

  //MMap the packet ring
  //TODO: Remember to close ring
  ps_header_start = mmap(0, RING_BLOCKSIZE*RING_BLOCK_NR, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  if (!ps_header_start)
  {
    perror("mmap");
    //return EXIT_FAILURE;
  }

  struct pollfd pfd;
  struct tpacket_hdr *header;
  uint64_t incomplete = 0;
  uint64_t dropped = 0;
  uint64_t i;
  pfd.fd = fd;
  pfd.revents = 0;
  //POLLRDNORM specced as == POLLIN and doesn't work
  //pfd.events = POLLIN|POLLRDNORM|POLLERR;
  pfd.events = POLLIN|POLLERR;

  header = (void *) ps_header_start;

#endif //MMAP_TECH

  if (fd < 0)
    exit(fd);

  while (limit > 0) {
    int err;
#ifdef MMAP_TECH
    while(!(header->tp_status & TP_STATUS_USER)){
      //1 file descriptor for infinte time
      err = poll(&pfd, 1, -1);
    }
    //First capture. Lets start the clock
    if(!clock_started){
      clock_started = 1;
      t_start = clock();
    }
    //limit--;
    //TODO:Packet processing
    
    while((header->tp_status & TP_STATUS_USER)){
      --limit;
      if (header->tp_status & TP_STATUS_COPY)
	incomplete++;
      else if (header->tp_status & TP_STATUS_LOSING)
	dropped++;
      else
	total_captured += header->tp_len;

      //Release frame back to kernel use
      header->tp_status = 0;

      //Update header point
      //header = (header + 1) & ((struct tpacket_hdr *)(RING_FRAME_NR -1 ));
      if(i>=RING_FRAME_NR-1)
	i = 0;
      else 
	i++;
      header = (void *) ps_header_start + i * RING_FRAME_SIZE;
    }
    //header = ((void *)(header + 1)) & ((void*)(RING_FRAME_NR -1));

#else 
    char buf[1600];
    //fprintf(stdout, "STATUS: (%d) \n", getpid());
    err = recv(fd, buf, BUFSIZE, 0);
#endif
    if(err < 0){
      perror("poll or read");
      //TODO: Handle error
      //break;
    }

    //err = read(fd, buf, sizeof(buf));
    //err = recv(fd, buf, BUFSIZE, MSG_WAITALL);
#ifdef OUTPUT
    if ((limit % 1000) == 0)
      fprintf(stdout, "(%d) \n", getpid());
#endif
  }
  //clock_t stop = clock();
  t_total = (double)(clock()-t_start)/CLOCKS_PER_SEC;

  fprintf(stderr, "%d: Received %d packets in %f seconds\n", getpid(), PACKET_NUM,t_total );

#ifdef MMAP_TECH
  fprintf(stderr, "Captured: %u bytes, with %u dropped and %u incomplete\n", total_captured, dropped, incomplete);
  munmap(header, RING_BLOCKSIZE*RING_BLOCK_NR);
#endif
  close(fd);
  exit(0);
}

int main(int argc, char **argp)
{
  //int fd, err;
  int i;

  if (argc != 3) {
    fprintf(stderr, "Usage: %s INTERFACE {hash|lb}\n", argp[0]);
    return EXIT_FAILURE;
  }

  if (!strcmp(argp[2], "hash"))
    fanout_type = PACKET_FANOUT_HASH;
  else if (!strcmp(argp[2], "lb"))
    fanout_type = PACKET_FANOUT_LB;
  else {
    fprintf(stderr, "Unknown fanout type [%s]\n", argp[2]);
    exit(EXIT_FAILURE);
  }

  device_name = argp[1];
  fanout_id = getpid() & 0xffff;

  for (i = 0; i < THREADS; i++) {
#ifdef THREADED
    pid_t pid = fork();

    switch (pid) {
      case 0:
	fanout_thread();

      case -1:
	perror("fork");
	exit(EXIT_FAILURE);
    }
#else	
    fanout_thread();
#endif //THREADED
  }

  for (i = 0; i < THREADS; i++) {
    int status;

    wait(&status);
  }

  return 0;
}
