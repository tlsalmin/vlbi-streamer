#ifndef ACTIVE_FILE_INDEX_H
#define ACTIVE_FILE_INDEX_H

/* For keeping track of files in use */
#include <pthread.h>

typedef enum
{
  FILESTATUS_RECORDING = 1,
  FILESTATUS_SENDING = 2
} afi_file_status;

typedef enum
{
  FH_ONDISK = 1,
  FH_MISSING = 2,
  FH_INMEM = 3,
  FH_BUSY = 4
} afi_fh_flags;

#define FI_CONDLOCK(x)                                                        \
  do                                                                          \
    {                                                                         \
      D("MUTEXNOTE: Locking cond mutex");                                     \
      pthread_mutex_lock(&((x)->wait_mutex));                                 \
    }                                                                         \
  while(0)
#define FI_CONDUNLOCK(x)                                                      \
  do                                                                          \
    {                                                                         \
      D("MUTEXNOTE: UnLocking cond mutex");                                   \
      pthread_mutex_unlock(&((x)->wait_mutex));                               \
    }                                                                         \
  while(0)
#define FI_READLOCK(x)                                                        \
  do                                                                          \
    {                                                                         \
      D("MUTEXNOTE: Locking read file mutex");                                \
      pthread_rwlock_rdlock(&((x)->augmentlock));                             \
    }                                                                         \
  while(0)
#define FI_WRITELOCK(x)                                                       \
  do                                                                          \
    {                                                                         \
      D("MUTEXNOTE: Locking write file mutex");                               \
      pthread_rwlock_wrlock(&((x)->augmentlock));                             \
    }                                                                         \
  while(0)
#define FIUNLOCK(x)                                                           \
  do                                                                          \
     {                                                                        \
       D("MUTEXNOTE: UnLocking file mutex");                                  \
       pthread_rwlock_unlock(&((x)->augmentlock));                            \
     }                                                                        \
  while(0)

#define FH_STATUS(ind) opt->fi->files[ind].status
#define FH_DISKID(ind) opt->fi->files[ind].diskid

#define SETFILESTATUS 		1
#define ADDTOFILESTATUS 	2
#define DELFROMFILESTATUS	3

#include "streamer.h"

struct file_index
{
  /* Protected by mainlock */
  int status;
  char *filename;
  int associations;
  struct file_index *next;
  /* Had to add this here, so live sendings can Probe the packet size */
  long unsigned packet_size;

  /* Protected by internal lock */
  struct fileholder *files;
  long unsigned n_packets;
  long unsigned n_files;
  long unsigned allocated_files;
  pthread_rwlock_t augmentlock;
  pthread_mutex_t wait_mutex;
  pthread_cond_t waiting;
};
struct fileholder
{
  int diskid;
  int status;
};
int remove_specific_from_fileholders(char *opt, int id);
int init_active_file_index();
int close_file_index(struct file_index *closing);
int close_active_file_index();
struct file_index *get_fileindex(char *name, int associate);
int disassociate(struct file_index *dis, int type);
struct file_index *add_fileindex(char *name, unsigned long n_files,
                                 int status, unsigned long packet_size);
int update_fileholder_status_wname(char *name, unsigned long filenum,
                                   int status, int action);
int update_fileholder_status(struct file_index *fi, unsigned long filenum,
                             int status, int action);
int remove_specific_from_fileholders(char *name, int recid);

/* Thread safe way of getting n_files */
long unsigned get_n_files(struct file_index *fi);
long unsigned get_n_packets(struct file_index *fi);
int add_file(struct file_index *fi, long unsigned id, int diskid, int status);
int get_status(struct file_index *fi);
unsigned long add_to_packets(struct file_index *fi,
                             unsigned long n_packets_to_add);
int mutex_free_wait_on_update(struct file_index *fi);
int wait_on_update(struct file_index *fi);
int wake_up_waiters(struct file_index *fi);
int full_metadata_update(struct file_index *fi, long unsigned *files,
                         long unsigned *packets, int *status);
unsigned long get_packet_size(struct file_index *fi);
int check_for_file_on_disk_and_wait_if_not(struct file_index *fi,
                                           unsigned long fileid);
#endif
