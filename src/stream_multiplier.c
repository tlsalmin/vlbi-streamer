#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#include <endian.h>
#include <net/if.h>

#define DEFPACKETSIZE 5016
#define DEFAULTSOCKET 46227
#define MAXMULTIPLY 128
extern char *optarg;
extern int optind, optopt;
void usage()
{
  fprintf(stdout, "Usage: stream_multiplier <target_ip1>:<port:,<target_ip2>:port,.. [ -p <port> ] [ -s <socket> ]\n");
}

int main(int argc, char** argv)
{
  int packetsize = DEFPACKETSIZE;
  int recv_socket = DEFAULTSOCKET;
  char ** targets = malloc(sizeof(char*)*MAXMULTIPLY);
  int n_targets =0;
  int ret;
  while((ret = getopt(argc, argv, "p:s:"))!= -1){
    switch (ret){
      case 'p':
	packetsize = atoi(optarg);
	break;
      case 's':
	recv_socket = atoi(optarg);
	break;
    }
  }
  if(argc -optind != 1){
    usage();
    exit(1);
  }
  argv +=optind;
  argc -=optind;
  
  /* Check if multiple addresses */
  if(strchr(argv[0], ',') ==NULL)
  {
    fprintf(stdout, "Only one target {%s\n", argv[0]);
    targets[0] = argv[0];
    n_targets=1;
  }
  else
  {
    fprintf(stdout, "Multiple targets:\n");
   }

  fprintf(stdout, "Recv socket: %d packets size: %d\n", recv_socket, packetsize);


  free(targets);
  return 0;
}
