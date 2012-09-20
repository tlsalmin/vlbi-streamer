#ifndef SIMPLEBUF_H
#define SIMPLEBUF_H
#include "streamer.h"
#define MMAP_NOT_SHMGET
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
#ifndef MMAP_NOT_SHMGET
  int shmid;
#endif
  struct fileholder* fh;
  struct fileholder* fh_def;
  //unsigned long file_seqnum;
  int ready_to_act;
  int bufnum;
  void * bufoffset;

  struct opt_s *opt_default;
  //struct opt_s *opt_old;
  char* filename_old;
  //unsigned long file_seqnum_old;
};
int sbuf_init(struct opt_s *opt, struct buffer_entity *be);
int sbuf_close(struct buffer_entity *be, void * stats);
void* sbuf_getbuf(struct buffer_entity *be, int** diff);
int sbuf_init_buf_entity(struct opt_s *opt, struct buffer_entity *be);
#endif
