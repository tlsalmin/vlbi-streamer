#ifndef ACTIVE_FILE_INDEX_H
#define ACTIVE_FILE_INDEX_H
/* For keeping track of files in use */

//init struct to 128 files at start
#define INITIAL_SIZE 128

#define FILESTATUS_RECORDING 	B(0)
#define FILESTATUS_SENDING 	B(1)

#define FH_ONDISK	B(0)
#define FH_MISSING	B(1)
#define FH_INMEM	B(2)
#define FH_BUSY		B(3)

#define FILOCK(x) pthread_mutex_lock(x->augmentlock)
#define FIUNLOCK pthread_mutex_unlock(x->augmentlock)

#define SETFILESTATUS 		1
#define ADDTOFILESTATUS 	2
#define DELFROMFILESTATUS	3

#include "streamer.h"
struct file_index{
  /* Protected by mainlock */
  int status;
  char* filename;
  int associations;
  struct file_index* next;

  /* Protected by internal lock */
  struct fileholder *files;
  int n_files;
  pthread_mutex_t * augmentlock;
  pthread_cond_t * waiting;
};
struct fileholder
{
  int diskid;
  int status;
};
void zero_fileholder(struct fileholder* fh);
int remove_specific_from_fileholders(char *opt, int id);
int init_active_file_index();
int close_file_index(struct file_index* closing);
int close_active_file_index();
struct file_index* get_fileindex(char * name);
int disassociate(struct file_index* dis);
struct file_index * add_fileindex(char * name, int n_files, int status);
int update_fileholder_status_wname(char * name, int filenum, int status, int action);
int update_fileholder_status(struct file_index * fi, int filenum, int status, int action);
inline void zero_fileholder(struct fileholder* fh);
int remove_specific_from_fileholders(char* name, int recid);
#endif
