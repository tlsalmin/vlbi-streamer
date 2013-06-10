#ifndef DATATYPES_H
#define DATATYPES_H

//#include <time.h>
#include "datatypes_common.h"
#include "streamer.h"

int init_header(void** target, struct opt_s* opt);
int check_and_fill(void * buffer, struct opt_s* opt, long fileid, int *expected_errors);
long header_match(void* target, void* match, struct opt_s * opt);
int get_sec_dif_from_buf(void * buffer, struct tm* time,struct opt_s* opt, int* res_err);

#endif
