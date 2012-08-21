#ifndef SIMPLEBUF_H
#define SIMPLEBUF_H
#include "streamer.h"
struct simplebuf{
  struct opt_s *opt;
  int diff;
  /* Used in init and close. Otherwise opts optbits is used 	*/
  /* TODO: Needs some refactoring to make more sensible		*/
  int optbits;
  int asyncdiff;
  void* buffer;
  int async_writes_submitted;
  int running;
  unsigned long file_seqnum;
  int ready_to_act;
  int bufnum;
  void * bufoffset;
};
int sbuf_init(struct opt_s *opt, struct buffer_entity *be);
int sbuf_close(struct buffer_entity *be, void * stats);
void* sbuf_getbuf(struct buffer_entity *be, int** diff);
int sbuf_init_buf_entity(struct opt_s *opt, struct buffer_entity *be);
#endif
