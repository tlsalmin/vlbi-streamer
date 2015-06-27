/**
 * Include this file if you want logging from a file that has a main in it */
#ifndef LOGGING_MAIN_H
#define LOGGING_MAIN_H
#include "logging.h"
__attribute__((constructor)) static void init_logging()
{
  file_out = stdout;
  file_err = stderr;
}
#endif /* LOGGING_MAIN_H */
