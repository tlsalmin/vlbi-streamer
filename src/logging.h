#ifndef LOGGING_H
#define LOGGING_H

#include <stdio.h>

#ifdef LOGGING_MAIN_H
#define SETASEXTERN
#else
#define SETASEXTERN extern
#endif

SETASEXTERN FILE *file_out;
SETASEXTERN FILE *file_err;

#define LOG(...) fprintf(file_out, __VA_ARGS__)
#define LOGERR(...) fprintf(file_err, __VA_ARGS__)
#ifdef DEBUG_OUTPUT
#define DEBUG_DEF(...) __VA_ARGS__
#define D(_fmt, _args...)                                                     \
  do                                                                          \
    {                                                                         \
      fprintf(file_out ,"%s:%d:%s(): " _fmt "\n",__FILE__,__LINE__,           \
              __func__, ##_args);                                             \
    }                                                                         \
  while(0)
#else
#define D(...)
#define DEBUG_DEF(...)
#endif
#define E(_fmt, _args...)                                                     \
  do                                                                          \
    {                                                                         \
      fprintf(file_err, "ERROR: %s:%d:%s(): " _fmt "\n",__FILE__,__LINE__,    \
              __func__, ## _args);                                            \
    }                                                                         \
  while(0)


#define DEBUG_OUTPUT_2 0
#define DD(...) if(DEBUG_OUTPUT_2)D(__VA_ARGS__)

#define CHECK_AND_EXIT(x) do { if(x == NULL){ E("Couldn't get any x so quitting"); pthread_exit(NULL); } } while(0)
#define INIT_ERROR return -1;
#define CHECK_ERR_CUST(x,y) do{if(y!=0){perror(x);E("ERROR:"x);return y;}else{D(x);}}while(0)
#define CHECK_ERR_CUST_QUIET(x,y) do{if(y!=0){perror(x);E("ERROR:"x);return -1;}}while(0)
#define CHECK_ERR(x) CHECK_ERR_CUST(x,err)
#define CHECK_ERR_AND_FREE(x,freethis) do{if(err!=0){perror(x);E("ERROR:"x);free(freethis);(freethis)=NULL;return -1;}else{D(x);}}while(0)
#define CHECK_ERR_QUIET(x) CHECK_ERR_CUST_QUIET(x,err)
#define CHECK_ERRP_CUST(x,y) do{if(y!=0){perror(x);E("ERROR:"x);pthread_exit(NULL);}else{D(x);}}while(0)
#define CHECK_ERRP(x) CHECK_ERRP_CUST(x,err)
#define CHECK_ERR_NONNULL(val,mes) do{if(val==NULL){perror(mes);E(mes);return -1;}else{D(mes);}}while(0)
#define CHECK_ERR_NONNULL_AUTO(val) do{if(val==NULL){perror("malloc "#val);E("malloc "#val);return -1;}else{D("malloc "#val);}}while(0)

#define CHECK_ERR_NONNULL_RN(val) do{if(val==NULL){perror("malloc "#val);E("malloc "#val);return NULL;}else{D("malloc "#val);}}while(0)
#define SILENT_CHECK_ERR_LTZ(x) do{if(err<0){perror(x);E(x);return -1;}}while(0)
#define SILENT_CHECK_ERRP_LTZ(x) do{if(err<0){perror(x);E(x);pthread_exit(NULL);}}while(0)
#define CHECK_LTZ(x,y) do{if(y<0){perror(x);E(x);return -1;}else{D(x);}}while(0)
#define CHECK_ERR_LTZ(x) CHECK_LTZ(x,err)
#define CALL_AND_CHECK(x,...)\
    err = x(__VA_ARGS__);\
  CHECK_ERR(#x);
#define CHECK_CFG_CUSTOM(x,y) do{if(y == CONFIG_FALSE){E(x);return -1;}else{D(x);}}while(0)
#define CHECK_CFG(x) CHECK_CFG_CUSTOM(x,err)

#endif
