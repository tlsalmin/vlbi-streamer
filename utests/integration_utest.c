#include <stdlib.h>
#include "../src/datatypes.h"
#include "common.h"
#include "../src/streamer.h"
#include "../src/config.h"
#include "string.h"

#define N_THREADS 1000 
#define N_FILES N_THREADS/2

void* testrun()
{
  return NULL;
}

int main()
{
  struct opt_s * opt = malloc(sizeof(struct opt_s)*N_THREADS);
  CHECK_ERR_NONNULL(opt, "malloc opt");

  free(opt);
  return 0;
}
