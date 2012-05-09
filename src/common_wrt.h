#ifndef COMMON_IO_FUNCS
#define COMMON_IO_FUNCS
#include "streamer.h"
/* TODO: Remove duplicates */
struct common_io_info{
  char * filename;
  int fd;
  long long offset;
  long long bytes_exchanged;
  INDEX_FILE_TYPE elem_size;
  int f_flags;
  INDEX_FILE_TYPE * indices;
  //int read;
  struct opt_s *opt;
  unsigned int optbits;
  INDEX_FILE_TYPE indexfile_count;
  void * extra_param;
};
int common_open_file(int *fd, int flags, char * filename, loff_t fallosize);
int common_write_index_data(const char * filename_orig, long unsigned elem_size, void *data, long unsigned count);
int common_handle_indices(struct common_io_info *ioi);
int common_w_init(struct opt_s* opt, struct recording_entity *re);
void get_io_stats(void * opt, void * st);
INDEX_FILE_TYPE * common_pindex(struct recording_entity *re);
unsigned long common_nofpacks(struct recording_entity *re);
int common_close(struct recording_entity * re, void * stats);
const char * common_wrt_get_filename(struct recording_entity *re);
int common_init_dummy(struct opt_s * opt, struct recording_entity *re);
int common_getfd(struct recording_entity *re);
void common_init_common_functions(struct opt_s *opt, struct recording_entity *re);
#ifdef HAVE_HUGEPAGES
char * find_hugetlbfs(char *fsmount, int len);
#endif
#endif
