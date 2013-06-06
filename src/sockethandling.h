#ifndef SOCKETHANDLING_H
#define SOCKETHANDLING_H
#define MODE_FROM_OPTS -1
/* TODO refactor name 	*/
struct socketopts
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
  unsigned long total_transacted_bytes;
  unsigned long incomplete;
  unsigned long files_sent; 
  unsigned long out_of_order;
  unsigned long *inc;

};
/* TODO: packets_sent needs to be set somewhere in a tcp stream.	*/
#define UDPS_EXIT do {spec_ops->opt->total_packets = st.n_packets_probed;D("Closing sender thread. Total sent %lu, Supposed to send: %lu",, st.packets_sent, spec_ops->opt->total_packets); if(se->be != NULL){set_free(spec_ops->opt->membranch, se->be->self);} se->stop(se);return 0;}while(0)
#define UDPS_EXIT_ERROR do {spec_ops->opt->total_packets = st.n_packets_probed; D("UDP_STREAMER: Closing sender thread. Left to send %lu, total sent: %lu",, st.packets_sent, spec_ops->opt->total_packets); if(se->be != NULL){set_free(spec_ops->opt->membranch, se->be->self);} spec_ops->opt->status = STATUS_ERROR;if(spec_ops->fd != 0){if(close(spec_ops->fd) != 0){E("Error in closing fd");}}return -1;}while(0)
int bind_port(struct addrinfo* si, int fd, int readmode, int do_connect);
int create_socket(int *fd, char * port, struct addrinfo ** servinfo, char * hostname, int socktype, struct addrinfo ** used, uint64_t optbits, char * device_name);
int socket_common_init_stuff(struct opt_s *opt, int mode, int* fd);
void close_socket(struct streamer_entity *se);
void free_the_buf(struct buffer_entity * be);
int close_streamer_opts(struct streamer_entity *se, void *stats);
void stop_streamer(struct streamer_entity *se);
  void reset_udpopts_stats(struct socketopts *spec_ops);
void *get_in_addr(struct sockaddr *sa);
int udps_wait_function(struct sender_tracking *st, struct opt_s* opt);
void bboundary_bytenum(struct streamer_entity* se, struct sender_tracking *st, unsigned long **counter);
void bboundary_packetnum(struct streamer_entity* se, struct sender_tracking *st, unsigned long **counter);
int generic_sendloop(struct streamer_entity * se, int do_wait, int(*sendcmd)(struct streamer_entity*, struct sender_tracking*), void(*buffer_boundary)(struct streamer_entity*, struct sender_tracking*, unsigned long **));
#endif
