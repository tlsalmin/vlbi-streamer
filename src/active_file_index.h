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

#define FILOCK(x) do {D("MUTEXNOTE: Locking file mutex"); pthread_mutex_lock(&((x)->augmentlock));} while(0)
#define FIUNLOCK(x) do {D("MUTEXNOTE: Unlocking file mutex"); pthread_mutex_unlock(&((x)->augmentlock));} while(0)

#define FH_STATUS(ind) opt->fi->files[ind].status
#define FH_DISKID(ind) opt->fi->files[ind].diskid

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
  long unsigned n_packets;
  long unsigned n_files;
  long unsigned allocated_files;
  pthread_mutex_t  augmentlock;
  pthread_cond_t  waiting;
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
struct file_index* get_fileindex(char * name, int associate);
int disassociate(struct file_index* dis, int type);
struct file_index * add_fileindex(char * name, unsigned long n_files, int status);
int update_fileholder_status_wname(char * name, unsigned long filenum, int status, int action);
int update_fileholder_status(struct file_index * fi, unsigned long filenum, int status, int action);
inline void zero_fileholder(struct fileholder* fh);
int remove_specific_from_fileholders(char* name, int recid);
/* Thread safe way of getting n_files */
long unsigned get_n_files(struct file_index* fi);
long unsigned get_n_packets(struct file_index* fi);
int add_file(struct file_index* fi, long unsigned id, int diskid, int status);
int get_status(struct file_index * fi);
unsigned long add_to_packets(struct file_index *fi, unsigned long n_packets_to_add);
int mutex_free_wait_on_update(struct file_index *fi);
int wait_on_update(struct file_index *fi);
int wake_up_waiters(struct file_index *fi);
#endif
