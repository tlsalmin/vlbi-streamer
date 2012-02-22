#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <unistd.h>

#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <arpa/inet.h>

#include <net/if.h>

static const char *device_name;
static int fanout_type;
static int fanout_id;
#define THREADED
#define OUTPUT
#define RING_BLOCKSIZE 16384
#define RING_FRAME_SIZE 8192
#define RING_BLOCK_NR 16
/*
#define RING_BLOCKSIZE 4096
#define RING_BLOCK_NR 4
#define RING_FRAME_SIZE 2048
*/

#define RING_FRAME_NR  RING_BLOCKSIZE / RING_FRAME_SIZE * RING_BLOCK_NR

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
#define PACKET_NUM 100000000
#define BUFSIZE 2^16
//From netcat
#define SA struct sockaddr	/* socket overgeneralization braindeath */
#define SAI struct sockaddr_in	/* ... whoever came up with this model */
#define IA struct in_addr	/* ... should be taken out and shot, */


static int setup_socket(void)
{
  int err, fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_IP));
  //int err, fd = socket(AF_PACKET, SOCK_RAW, 16);
  //int err, fd = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_IP));
  struct sockaddr_ll ll;
  struct ifreq ifr;
  //for MMAP-buffer
  struct tpacket_req req;
  int fanout_arg;
  int o_rcvbuf;

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
  ll.sll_protocol = ETH_P_ALL;
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
#endif
  /*
  req.tp_block_size= 4096;
    req.tp_frame_size= 2048;
    req.tp_block_nr  = 4;
    req.tp_frame_nr  = 8;
  //Set a ringbuffer for mmap
  */
  req.tp_block_size = RING_BLOCKSIZE;
  req.tp_frame_size = RING_FRAME_SIZE;
  req.tp_block_nr = RING_BLOCK_NR;
  req.tp_frame_nr = RING_FRAME_NR;
  err = setsockopt(fd, SOL_PACKET, PACKET_RX_RING, (void*) &req, sizeof(req));
  //err = setsockopt(fd, SOL_PACKET, PACKET_RX_RING, (void *) &req, sizeof(req));
  if (err) {
    perror("PACKET_RX_RING failed");
    return EXIT_FAILURE;
  }
  /*
  o_rcvbuf = 131071;
  err = setsockopt(fd, SOL_PACKET, SO_RCVBUF, &o_rcvbuf, sizeof(o_rcvbuf));
  if (err) {
    perror("RCVBUF augment failed");
    return EXIT_FAILURE;
  }
  */

  return fd;
}

static void fanout_thread(void)
{
  int fd = setup_socket();
  int limit = PACKET_NUM;
  int x = sizeof(SA);
  SAI * remend =NULL;

  if (fd < 0)
    exit(fd);

  while (limit-- > 0) {
    char buf[1600];
    int err;

    //err = read(fd, buf, sizeof(buf));
    //err = recv(fd, buf, BUFSIZE, MSG_WAITALL);
    err = recv(fd, buf, BUFSIZE, 0);
    if (err < 0) {
      perror("read");
      exit(EXIT_FAILURE);
    }
#ifdef OUTPUT
    if ((limit % 10) == 0)
      fprintf(stdout, "(%d) \n", getpid());
#endif
  }

  fprintf(stdout, "%d: Received %d packets\n", getpid(), PACKET_NUM);

  close(fd);
  exit(0);
}

int main(int argc, char **argp)
{
  int fd, err;
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
    pid_t pid = fork();

    switch (pid) {
      case 0:
	fanout_thread();

      case -1:
	perror("fork");
	exit(EXIT_FAILURE);
    }
  }

  for (i = 0; i < THREADS; i++) {
    int status;

    wait(&status);
  }

  return 0;
}
