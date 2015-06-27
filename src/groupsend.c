#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "config.h"
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
#include <sys/mman.h>           //for MMAP and poll
#include <sys/poll.h>
#include <netdb.h>

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

#include "logging_main.h"

#define B(x) (1l << x)
#define UDP_SOCKET	B(0)
#define TCP_SOCKET	B(1)

#define TCP_BONUS 256

#define STARTPORT 2222

int def = 0, defcheck = 0, len = 0, packet_size = 0;
int opts;

/* Cleaned up from round 1	 	*/
int connect_to_c(const char *t_target, const char *t_port, int *fd)
{
  int err;
  struct addrinfo hints, *res, *p;
  memset(&hints, 0, sizeof(struct addrinfo));

  if (opts & TCP_SOCKET)
    hints.ai_socktype = SOCK_STREAM;
  else
    hints.ai_socktype = SOCK_DGRAM;
  int gotthere;
  hints.ai_family = AF_UNSPEC;
  err = getaddrinfo(t_target, t_port, &hints, &res);
  if (err != 0)
    {
      E("Error in getaddrinfo: %s for %s:%s", gai_strerror(err), t_target,
        t_port);
      return -1;
    }
  for (p = res; p != NULL; p = p->ai_next)
    {
      gotthere = 0;
      *fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
      if (*fd < 0)
        {
          E("Socket to client");
          continue;
        }

      err = connect(*fd, res->ai_addr, res->ai_addrlen);
      if (err < 0)
        {
          close(*fd);
          E("Connect to client");
          continue;
        }
      int yes = 1;
      err = setsockopt(*fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
      gotthere = 1;
      break;
    }
  if (gotthere == 0)
    return -1;

  freeaddrinfo(res);
  if (def == 0)
    {
      def = packet_size;
      len = sizeof(def);
      while (err == 0)
        {
          def = def << 1;
          err = setsockopt(*fd, SOL_SOCKET, SO_SNDBUF, &def, (socklen_t) len);
          if (err == 0)
            {
            }
          err =
            getsockopt(*fd, SOL_SOCKET, SO_SNDBUF, &defcheck,
                       (socklen_t *) & len);
          if (defcheck != (def << 1))
            {
              LOG("Limit reached. Final size is %d Bytes\n", defcheck);
              def = defcheck;
              break;
            }
        }
    }
  else
    {
      err = setsockopt(*fd, SOL_SOCKET, SO_SNDBUF, &def, (socklen_t) len);
      if (err != 0)
        E("Error in setting SO_SNDBUF");
    }

  return 0;
}

void usage()
{
  LOG("USAGE: groupsend <comma separated list of targets> <streams> <packet_size> <time>\n");
  exit(-1);
}

#define MAX_TARGETS 16
#define HOSTNAME_MAXLEN    256

int main(int argc, char **argv)
{
  char *targets[MAX_TARGETS], *port;
  int streams;
  int dont_run = 0;
  struct timespec tval_start, tval;
  int runtime;
  int portn;
  size_t sendbuffer;
  int smallest = 0;
  int n_targets = 0;
  int ret;
  opts = 0;
  uint64_t min;

  opts |= UDP_SOCKET;
  while ((ret = getopt(argc, argv, "tp:")) != -1)
    {
      switch (ret)
        {
        case 't':
          opts &= ~UDP_SOCKET;
          opts |= TCP_SOCKET;
          break;
        case 'p':
          portn = atoi(optarg);
          if (portn <= 0 || portn >= 65536)
            {
              E("Illegal port %d", portn);
              usage();
            }
          break;
        default:
          E("Unknown parameter %c", ret);
          usage();
        }
    }
  argv += (optind - 1);
  argc -= (optind - 1);

  if (argc != 5)
    usage();

  char *temp = argv[1], *temp2;
  int i = 0, err = 0;
  while ((temp2 = index(temp, ',')) != NULL)
    {
      targets[n_targets] = malloc(sizeof(char) * HOSTNAME_MAXLEN);
      memset(targets[n_targets], 0, sizeof(char) * HOSTNAME_MAXLEN);
      memcpy(targets[n_targets], temp, temp2 - temp);
      n_targets++;
      temp = temp2 + 1;
    }
  targets[n_targets] = temp;
  n_targets++;
  for (i = 0; i < n_targets; i++)
    {
      LOG("Target %d is %s\n", i, targets[i]);
    }
  streams = atoi(argv[2]);
  packet_size = atoi(argv[3]);

  runtime = atoi(argv[4]);
  uint64_t sent[streams];
  memset(sent, 0, sizeof(uint64_t) * streams);

  int *sockets = (int *)malloc(sizeof(int) * streams);
  /* Lets shove a multiple of packet_size for TCP to get better throughput      */
  if (opts & TCP_SOCKET)
    sendbuffer = packet_size * TCP_SOCKET;
  else
    sendbuffer = packet_size;

  void *buf = malloc(sendbuffer);

  port = malloc(sizeof(char) * 8);

  LOG("Connecting");
  for (i = 0; i < streams; i++)
    {
      memset(port, 0, sizeof(char) * 8);
      sprintf(port, "%d", portn + i);
      err = connect_to_c(targets[i % n_targets], port, &sockets[i]);
      if (err != 0)
        {
          E("Error in connect");
          dont_run = 1;
          break;
        }
    }
  LOG("Streams done\n");

  clock_gettime(CLOCK_REALTIME, &tval_start);

  LOG("Into send-loop\n");
  while (err >= 0 && dont_run != 1)
    {
      min = UINT64_MAX;
      for (i = 0; i < streams; i++)
        {
          if (sent[i] < min)
            {
              min = sent[i];
              smallest = i;
            }
        }
      err = send(sockets[smallest], buf, sendbuffer, 0);
      if (err > 0)
        sent[smallest] += err;
      else if (err < 0)
        {
          perror("hur");
          break;
        }
      clock_gettime(CLOCK_REALTIME, &tval);
      if (tval.tv_sec - tval_start.tv_sec > runtime)
        {
          LOG("Times up!\n");
          break;
        }
    }

  free(buf);
  for (i = 0; i < streams; i++)
    {
      shutdown(sockets[i], SHUT_RDWR);
      close(sockets[i]);
    }
  for (i = 0; i < n_targets - 1; i++)
    {
      free(targets[i]);
    }
  free(sockets);
  for (i = 0; i < streams; i++)
    LOG("Sent %ld bytes to port %d\n", sent[i], portn + i);
  return 0;
}
