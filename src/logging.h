#ifndef LOGGING_H
#define LOGGING_H
#if(LOG_TO_FILE)
#define LOGTONORMAL logfile
#define LOGTOERR logfile
#else
#define LOGTONORMAL stdout
#define LOGTOERR stderr
#endif
#define LOG(...) fprintf(LOGTONORMAL, __VA_ARGS__)
#define LOGERR(...) fprintf(LOGTOERR, __VA_ARGS__)
#define D(str, ...)\
    do { if(DEBUG_OUTPUT) fprintf(LOGTONORMAL,"%s:%d:%s(): " str "\n",__FILE__,__LINE__,__func__ __VA_ARGS__); } while(0)
#define E(str, ...)\
    do { fprintf(LOGTOERR,"ERROR: %s:%d:%s(): " str "\n",__FILE__,__LINE__,__func__ __VA_ARGS__ );perror("Error message"); } while(0)


#define DEBUG_OUTPUT_2 0
#define DD(str, ...) if(DEBUG_OUTPUT_2)D(str, __VA_ARGS__)

#define CHECK_AND_EXIT(x) do { if(x == NULL){ E("Couldn't get any x so quitting"); pthread_exit(NULL); } } while(0)
#define INIT_ERROR return -1;
#define CHECK_ERR_CUST(x,y) do{if(y!=0){perror(x);E("ERROR:"x);return y;}else{D(x);}}while(0)
#define CHECK_ERR_CUST_QUIET(x,y) do{if(y!=0){perror(x);E("ERROR:"x);return -1;}}while(0)
#define CHECK_ERR(x) CHECK_ERR_CUST(x,err)
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

#endif
