#ifndef COMMON_IO_FUNCS
#define COMMON_IO_FUNCS
#include "streamer.h"
int common_open_file(int *fd, int flags, char * filename, loff_t fallosize);
int common_write_index_data(const char * filename_orig, int elem_size, void *data, int count);
int common_handle_indices(const char *filename_orig, INDEX_FILE_TYPE * elem_size, void * pindex, INDEX_FILE_TYPE *count);
#endif
