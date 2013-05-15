#ifndef SOCKETHANDLING_H
#define SOCKETHANDLING_H
#define MODE_FROM_OPTS -1
/* TODO refactor name 	*/
struct udpopts
{
  int fd;
  int tcp_fd;
  int fd_send;
  struct opt_s* opt;
  //long unsigned int * cumul;
  //TODO: REmove these two
  struct sockaddr_storage sin;
  socklen_t sin_l;
  struct sockaddr_in *sin_send;
  struct addrinfo *p;
  struct addrinfo *p_send;
  struct addrinfo *servinfo;
  struct addrinfo *servinfo_simusend;
  size_t sinsize;
  int wrongsizeerrors;

  unsigned long missing;
  unsigned long total_captured_bytes;
  unsigned long incomplete;
  unsigned long files_sent; 
  unsigned long out_of_order;
};
int bind_port(struct addrinfo* si, int fd, int readmode, int do_connect);
int create_socket(int *fd, char * port, struct addrinfo ** servinfo, char * hostname, int socktype, struct addrinfo ** used, uint64_t optbits);
int socket_common_init_stuff(struct opt_s *opt, int mode, int* fd);
void close_socket(struct streamer_entity *se);
void free_the_buf(struct buffer_entity * be);
int close_streamer_opts(struct streamer_entity *se, void *stats);
void stop_streamer(struct streamer_entity *se);
  void reset_udpopts_stats(struct udpopts *spec_ops);
void *get_in_addr(struct sockaddr *sa);
#endif
