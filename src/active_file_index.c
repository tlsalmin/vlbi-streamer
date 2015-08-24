#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#include "active_file_index.h"
#include "logging.h"
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

//init struct to 128 files at start
#define INITIAL_SIZE 128

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

struct file_index
{
  /* Protected by mainlock */
  int status;
  char *filename;
  size_t associations;
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

struct file_index *files = NULL;
pthread_mutex_t mainlock = PTHREAD_MUTEX_INITIALIZER;

#define MAINLOCK                                                              \
  do                                                                          \
    {                                                                         \
      if (pthread_mutex_lock(&mainlock) != 0)                                 \
        {                                                                     \
          perror("active_file_index mainlock");                               \
        }                                                                     \
    }                                                                         \
  while(0)
#define MAINUNLOCK                                                            \
  do                                                                          \
    {                                                                         \
      if (pthread_mutex_unlock(&mainlock) != 0)                               \
        {                                                                     \
          perror("active_file_index mainlock");                               \
        }                                                                     \
    }                                                                         \
  while(0)

int afi_init()
{
  files = NULL;
  LOG("Initializing active file index\n");
  if (pthread_mutex_init(&mainlock, NULL) != 0)
    {
      E("Failed to initialize active_file_index lock: %s", strerror(errno));
      return -1;
    }
  return 0;
}

static void free_file_index(struct file_index *fi)
{
  if (fi)
    {
      free(fi->files);
      pthread_mutex_destroy(&(fi->wait_mutex));
      pthread_cond_destroy(&(fi->waiting));
      pthread_rwlock_destroy(&(fi->augmentlock));
      free(fi->filename);
      free(fi);
    }
}

static void add_to_list_mutex_free(struct file_index *fi)
{
  assert(fi != NULL && fi->next == NULL);

  if (!files)
    {
      files = fi;
    }
  else
    {
      struct file_index *iter = files;

      while (iter->next != NULL)
        {
          iter = iter->next;
        }
      iter->next = fi;
    }
}

static void add_to_list(struct file_index *fi)
{
  assert(fi != NULL && fi->next == NULL);

  MAINLOCK;
  add_to_list_mutex_free(fi);
  MAINUNLOCK;
}

static void remove_from_list_mutex_free(struct file_index *fi)
{
  if (fi == files)
    {
      files = fi->next;
      fi->next = NULL;
    }
  else
    {
      struct file_index *iter = files;
      bool found = false;

      while (iter->next != NULL)
        {
          if (iter->next == fi)
            {
              iter->next = fi->next;
              fi->next = NULL;
              found = true;
              break;
            }
          iter = iter->next;
        }
      if (found == false)
        {
          E("Could not find %s from file index", fi->filename);
        }
    }
}

static void remove_from_list(struct file_index *fi)
{
  assert(fi != NULL);

  MAINLOCK;
  remove_from_list_mutex_free(fi);
  MAINUNLOCK;
}

static int close_file_index_mutex_free(struct file_index *fi)
{
  int retval = 0;

  assert(fi != NULL);
  if (fi->associations != 0)
    {
      E("Closing file which still has associations: %s!", fi->filename);
      retval = -1;
    }
  remove_from_list_mutex_free(fi);

  free_file_index(fi);

  return retval;
}

int close_file_index(struct file_index *fi)
{
  int ret = 0;

  MAINLOCK;
  if (close_file_index_mutex_free(fi) != 0)
    {
      ret = -1;
    }
  MAINUNLOCK;

  return ret;
}

int add_file_mutexfree(struct file_index *fi, long unsigned id, int diskid,
                       int status)
{
  if (id > fi->allocated_files)
    {
      void *tempfiles = fi->files;
      while (fi->allocated_files < id)
        {
          fi->allocated_files = fi->allocated_files << 1;
        }
      if ((fi->files =
           realloc(fi->files,
                   fi->allocated_files * sizeof(struct fileholder))) == NULL)
        {
          E("Realloc failed!");
          fi->files = tempfiles;
          return -1;
        }
    }
  if (fi->n_files > id)
    {
      D("Warning: Adding file which already exists with id %ld", id);
    }
  else
    {
      fi->n_files++;
    }
  fi->files[id].diskid = diskid;
  fi->files[id].status = status;
  D("Added file %lu of %s with status FH_MISSING: %d", id, fi->filename,
    status & FH_MISSING);
  return 0;
}

int afi_add_file(struct file_index *fi, long unsigned id, int diskid, int status)
{
  int err;
  FI_WRITELOCK(fi);
  err = add_file_mutexfree(fi, id, diskid, status);
  FIUNLOCK(fi);
  return err;
}

int afi_close()
{
  int err, retval = 0;

  while (files)
    {
      struct file_index *temp = files;

      files = temp->next;
      temp->next = NULL;
      err = close_file_index(temp);
      if (err != 0)
        {
          E("Error in closing file index!");
          retval--;
        }
    }
  assert(files == NULL);
  if (pthread_mutex_destroy(&mainlock) != 0)
    {
      perror("active_file_index mainlock close");
    }
  return retval;
}

static struct file_index *afi_get_mutex_free(const char *name,
                                                   bool associate)
{
  struct file_index *temp = files;

  assert(name != NULL);

  D("Trying to find %s", name);
  while (temp != NULL)
    {
      if (strcmp(temp->filename, name) == 0)
        {
          temp->associations += (associate == true);
          D("%s found!", name);
          return temp;
        }
      temp = temp->next;
    }
  D("%s Not found", name);
  return NULL;
}

struct file_index *afi_get(const char *name, bool associate)
{
  struct file_index *temp;

  MAINLOCK;
  temp = afi_get_mutex_free(name, associate);
  MAINUNLOCK;

  return temp;
}

#define REC_OR_SEND(_status) ((_status == AFI_RECORD) ? \
                              "recording" : "sending")

/* Returns existing file_index if found or creates new if not */
struct file_index *afi_add(const char *name, unsigned long n_files,
                           afi_file_status status, unsigned long packet_size)
{
  struct file_index *fi;

  assert(name != NULL);
  D("Adding new %s file %s to index with %zu files with packet_size %zu",
    REC_OR_SEND(status), name, n_files, packet_size);

  MAINLOCK;
  fi = afi_get_mutex_free(name, false);
  if (fi)
    {
      MAINUNLOCK;

      FI_WRITELOCK(fi);
      D("File %s already exists in index", name);
      if (status & AFI_RECORD)
        {
          if (fi->status & AFI_RECORD)
            {
              E("Cant give recording status to a file thats already being "
                "recorded");
              FIUNLOCK(fi);
              return NULL;
            }
          else
            {
              D("Adding status recording to existing file index");
              fi->status |= AFI_RECORD;
            }
        }
      else if (status & AFI_SEND)
        {
          fi->status |= AFI_SEND;
        }
      fi->associations++;
      FIUNLOCK(fi);
      return fi;
    }

  D("File %s really is new", name);
  fi = (struct file_index *)calloc(1, sizeof(*fi));
  if (!fi)
    {
      E("Failed to allocate for new file index named %s", name);
      goto error_exit;
    }

  pthread_rwlock_init(&(fi->augmentlock), NULL);
  pthread_mutex_init(&(fi->wait_mutex), NULL);
  pthread_cond_init(&(fi->waiting), NULL);

  fi->filename = strdup(name);
  if (!fi->filename)
    {
      E("OOM: Failed to copy filename to file index: %s", name);
      goto error_exit;
    }

  fi->n_files = n_files;

  /* 0 means its a new recording and we don't know how many we're setting yet */
  fi->allocated_files = (n_files != 0) ? n_files : INITIAL_SIZE;

  fi->files = (struct fileholder *) calloc(fi->allocated_files,
                                           sizeof(struct fileholder));
  if (!fi->files)
    {
      E("OOM for file index %s", name);
      goto error_exit;
    }
  fi->status = status;
  fi->packet_size = packet_size;

  D("File %s added to index", name);

  fi->associations = 1;

  add_to_list_mutex_free(fi);

  MAINUNLOCK;
  return fi;

error_exit:
  MAINUNLOCK;
  free_file_index(fi);
  return NULL;
}

int afi_disassociate(struct file_index *fi, afi_file_status type)
{
  int err = 0;

  assert(fi);

  /*Handling associations requires mainlock */
  FI_WRITELOCK(fi);
  D("Disassociating with %s which has %zu associations", fi->filename,
    fi->associations);

  assert(fi->associations > 0);
  fi->associations--;

  if (fi->associations == 0)
    {
      D("%s has no more associations. Closing it", fi->filename);
      FIUNLOCK(fi);
      return close_file_index_mutex_free(fi);
    }
  if (type & AFI_RECORD)
    {
      assert(fi->status & AFI_RECORD);
      fi->status &= ~AFI_RECORD;
    }
  FIUNLOCK(fi);

  if (type & AFI_RECORD)
    {
      afi_wake_up(fi);
    }

  return err;
}

int mutex_free_afi_wait_on_update(struct file_index *fi)
{
  if (fi->status & AFI_RECORD)
    {
      return pthread_cond_wait(&(fi->waiting), &(fi->wait_mutex));
    }
  else
    {
      E("Asking wait on file thats not being recorded! %s", fi->filename);
      return -1;
    }
}

int afi_wait_on_update(struct file_index *fi)
{
  int retval;
  FI_CONDLOCK(fi);
  retval = mutex_free_afi_wait_on_update(fi);
  FI_CONDUNLOCK(fi);
  return retval;
}

int mutex_free_afi_wake_up(struct file_index *fi)
{
  return pthread_cond_broadcast(&(fi->waiting));
}

int afi_wake_up(struct file_index *fi)
{
  int retval;
  FI_CONDLOCK(fi);
  retval = mutex_free_afi_wake_up(fi);
  FI_CONDUNLOCK(fi);
  return retval;
}

void and_action(struct file_index *fi, unsigned long filenum, int status,
                int action)
{
  switch (action)
    {
    case AFI_ADD_TO_STATUS:
      fi->files[filenum].status |= status;
      break;
    case AFI_DEL_FROM_STATUS:
      fi->files[filenum].status &= ~status;
      break;
    default:
      E("Unknown action");
    }
}

static int afi_update_fh_wname(char *name, unsigned long filenum,
                               int status, int action)
{
  struct file_index *modding = afi_get(name, false);
  if (modding == NULL)
    {
      E("No file with name %s found", name);
      return -1;
    }
  D("Got filename for update");
  FI_WRITELOCK(modding);
  and_action(modding, filenum, status, action);
  FIUNLOCK(modding);
  return 0;
}

void afi_update_fh(struct file_index *fi, unsigned long filenum, int status,
                   afi_update_action action)
{
  FI_WRITELOCK(fi);
  and_action(fi, filenum, status, action);
  FIUNLOCK(fi);
}

int update_fileholder(struct file_index *fi, unsigned long filenum,
                      int status, int action, int recid)
{
  FI_WRITELOCK(fi);
  and_action(fi, filenum, status, action);
  fi->files[filenum].diskid = recid;
  FIUNLOCK(fi);
  return 0;
}

int afi_mark_recid_missing(char *name, int recid)
{
  unsigned long i;
  struct file_index *fi = afi_get(name, false);

  if (fi == NULL)
    {
      E("file index %s not found", name);
      return -1;
    }
  FI_WRITELOCK(fi);
  for (i = 0; i < fi->n_files; i++)
    {
      if (fi->files[i].diskid == recid)
        {
          fi->files[i].diskid = -1;
          fi->files[i].status &= ~FH_ONDISK;
          fi->files[i].status |= FH_MISSING;
        }
    }
  FIUNLOCK(fi);

  return 0;
}

long unsigned afi_get_n_files(struct file_index *fi)
{
  long unsigned ret;
  FI_READLOCK(fi);
  ret = fi->n_files;
  FIUNLOCK(fi);
  return ret;
}

long unsigned afi_get_n_packets(struct file_index *fi)
{
  long unsigned ret;
  FI_READLOCK(fi);
  ret = fi->n_packets;
  FIUNLOCK(fi);
  return ret;
}

unsigned long afi_add_to_packets(struct file_index *fi,
                             unsigned long n_packets_to_add)
{
  unsigned long n_packets;
  FI_WRITELOCK(fi);
  fi->n_packets += n_packets_to_add;
  n_packets = fi->n_packets;
  FIUNLOCK(fi);
  return n_packets;
}

afi_file_status afi_get_status(struct file_index *fi)
{
  int ret;
  FI_READLOCK(fi);
  ret = fi->status;
  FIUNLOCK(fi);
  return ret;
}

void afi_update_metadata(struct file_index *fi, long unsigned *files,
                         long unsigned *packets, int *status)
{
  FI_READLOCK(fi);
  if (files)
    {
      *files = fi->n_files;
    }
  if (packets)
    {
      *packets = fi->n_packets;
    }
  if (status)
    {
      *status = fi->status;
    }
  FIUNLOCK(fi);
}

unsigned long afi_get_packet_size(struct file_index *fi)
{
  unsigned long temp;
  FI_READLOCK(fi);
  temp = fi->packet_size;
  FIUNLOCK(fi);
  return temp;
}

int afi_wait_on_file(struct file_index *fi, unsigned long fileid)
{
  FI_READLOCK(fi);
  while (!(fi->files[fileid].status & FH_ONDISK))
    {
      if (fi->files[fileid].status & FH_MISSING)
        {
          E("Recording %s File %ld gone missing so wont wait for it",
            fi->filename, fileid);
          FIUNLOCK(fi);
          return -1;
        }
      D("Recording %s is busy with file %ld. Not acquiring write point for it",
        fi->filename, fileid);
      FI_CONDLOCK(fi);
      FIUNLOCK(fi);
      mutex_free_afi_wait_on_update(fi);
      FI_CONDUNLOCK(fi);
      FI_READLOCK(fi);
    }
  FIUNLOCK(fi);
  return 0;
}

void afi_single_all_found(struct file_index *fi, size_t disk_id)
{
  size_t i;

  assert(fi != NULL);

  FI_WRITELOCK(fi);
  for(i = 0; i < fi->n_files; i++)
    {
      struct fileholder* fh = &fi->files[i];

      fh->status &= ~FH_MISSING;
      fh->status |= FH_ONDISK;
      fh->diskid = disk_id;
    }
  FIUNLOCK(fi);
}

void afi_set_found(struct file_index *fi, size_t file, size_t disk_id)
{
  struct fileholder *fh;

  FI_WRITELOCK(fi);
  assert(fi && fi->n_files <= file);
  fh = &(fi->files[file]);
  fh->status &= ~FH_MISSING;
  fh->status |= FH_ONDISK;
  fh->diskid = disk_id;
  FIUNLOCK(fi);
}

size_t afi_get_disk_id(struct file_index *fi, size_t file)
{
  size_t disk_id;

  FI_READLOCK(fi);
  assert(fi != NULL && fi->n_files <= file);
  disk_id = fi->files[file].diskid;
  FIUNLOCK(fi);

  return disk_id;
}

const char *afi_get_filename(struct file_index *fi)
{
  assert(fi != NULL);

  // Filename isn't mutable so not locking here.
  return fi->filename;
}

void afi_set_all_missing(struct file_index *fi)
{
  size_t i;

  assert(fi);

  FI_WRITELOCK(fi);
  for (i = 0; i < fi->n_files; i++)
    {
      struct fileholder *fh = &fi->files[i];

      fh->diskid = -1;
      fh->status = FH_MISSING;
    }
  FIUNLOCK(fi);
}

bool afi_is_missing(struct file_index *fi, size_t file_id)
{
  bool val;

  FI_READLOCK(fi);
  assert(fi != NULL && file_id <= fi->n_files);
  val = !!(fi->files[file_id].status & FH_MISSING);
  FIUNLOCK(fi);

  return val;
}

int afi_start_loading(struct file_index *fi, size_t file_id, bool in_loading)
{
  struct fileholder *fh;

  FI_READLOCK(fi);
  assert(fi != NULL && fi->n_files <= file_id);
  fh = &fi->files[file_id];
  if (!(fh->status & FH_ONDISK))
    {
      if (fh->status & (FH_BUSY))
        {
          if (in_loading)
            {
              FIUNLOCK(fi);
              return 1;
            }
          else
            {
              while ((fh->status & (FH_BUSY)) &&
                     !(fh->status & FH_ONDISK))
                {
                  int err;

                  FI_CONDLOCK(fi);
                  FIUNLOCK(fi);
                  err = mutex_free_afi_wait_on_update(fi);
                  FI_CONDUNLOCK(fi);
                  if (err != 0)
                    {
                      E("on wait on update");
                      return -1;
                    }
                  FI_READLOCK(fi);
                }
            }
        }
      else
        {
          D("Not on disk so not loading");
          FIUNLOCK(fi);
          return 1;
        }
    }
  FIUNLOCK(fi);

  return 0;
}
