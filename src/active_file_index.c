#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "active_file_index.h"
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

struct file_index * files;
pthread_mutex_t mainlock;

#define INITMAINLOCK do { if(pthread_mutex_init(&mainlock, NULL) != 0){perror("active_file_index mainlock init");}} while(0)
#define MAINLOCK do { if(pthread_mutex_lock(&mainlock) != 0) { perror("active_file_index mainlock");} } while(0)
#define MAINUNLOCK do { if(pthread_mutex_unlock(&mainlock) != 0) { perror("active_file_index mainlock");} } while(0)
#define CLOSEMAINLOCK do { if(pthread_mutex_destroy(&mainlock) != 0) {perror("active_file_index mainlock close");}} while(0)

int init_active_file_index()
{
  //pthread_spin_init(&mainlock, PTHREAD_PROCESS_SHARED);
  INITMAINLOCK;
  files = NULL;
  return 0;
}
int close_file_index_mutex_free(struct file_index* closing)
{
  int retval=0;
  struct file_index* temp;
  int notfound=0;
  if(closing->associations != 0){
    E("Closing file which still has associations: %s!",, closing->filename);
    retval=-1;
  }
  if(closing == NULL){
    E("closing is null!");
    return -1;
  }
  temp = files;
  if(temp == NULL){
    E("No files in index! Weird but no reason to exit!");
  }
  else if(temp == closing){
    files = closing->next;
  }
  else
  {
    while(temp->next != closing){
      if(temp->next == NULL){
	E("Closing file not found in file index!");
	notfound =1;
	break;
      }
      temp = temp->next;
    }
    if(notfound == 0)
      temp->next = closing->next;
  }
  free(closing->files);
  pthread_mutex_destroy(&(closing->wait_mutex));
  pthread_cond_destroy(&(closing->waiting));
  pthread_rwlock_destroy(&(closing->augmentlock));
  free(closing->filename);
  free(closing);
  return retval;
}
int close_file_index(struct file_index* closing)
{

  MAINLOCK;
  if (close_file_index_mutex_free(closing) != 0){
    MAINUNLOCK;
    return -1;
  }
  MAINUNLOCK;
  return 0;
}
int add_file_mutexfree(struct file_index* fi, long unsigned id, int diskid, int status)
{
  if(id > fi->allocated_files){
    void* tempfiles = fi->files;
    while(fi->allocated_files < id)
    {
      fi->allocated_files = fi->allocated_files << 1;
    }
    if((fi->files = realloc(fi->files, fi->allocated_files*sizeof(struct fileholder))) == NULL){
      E("Realloc failed!");
      fi->files = tempfiles;
      FIUNLOCK(fi);
      return -1;
    }
  }
  if(fi->n_files > id)
  {
    D("Warning: Adding file which already exists with id %ld",, id);
  }
  else{
    fi->n_files++;
  }
  fi->files[id].diskid = diskid;
  fi->files[id].status = status;
  D("Added file %ld of %s with status FH_MISSING: %ld",, id, fi->filename, status & FH_MISSING );
  return 0;
}
int add_file(struct file_index* fi, long unsigned id, int diskid, int status)
{
  int err;
  FI_WRITELOCK(fi);
  err = add_file_mutexfree(fi,id,diskid,status);
  FIUNLOCK(fi);
  return err;
}
int close_active_file_index()
{
  int err,retval=0;
  struct file_index* temp = files;
  struct file_index* temp2;
  while(temp != NULL){
    temp2 = temp;
    temp = temp->next;
    err = close_file_index(temp2);
    if(err != 0){
      E("Error in closing file index!");
      retval--;
    }
  }
  files = NULL;
  CLOSEMAINLOCK;
  return retval;
}
struct file_index* get_fileindex_mutex_free(char * name, int associate)
{
  D("Trying to find %s",, name);
  struct file_index * temp = files;
  while(temp != NULL){
    if(strncmp(temp->filename, name,FILENAME_MAX) == 0){
      if(associate == 1){
	temp->associations++;
      }
      D("%s found!",, name);
      return temp;
    }
    temp= temp->next;
  }
  D("%s Not found",, name);
  return NULL;
}
struct file_index* get_fileindex(char * name, int associate)
{
  struct file_index * temp;
  MAINLOCK;
  temp = get_fileindex_mutex_free(name, associate);
  MAINUNLOCK;
  return temp;
}
inline struct file_index* get_last()
{
  struct file_index* returnable = files;
  if(returnable == NULL)
    return NULL;
  while(returnable->next != NULL)
    returnable = returnable->next;
  return returnable;
}
/* Returns existing file_index if found or creates new if not */
struct file_index * add_fileindex(char * name, unsigned long n_files, int status, unsigned long packet_size)
{
  D("Adding new file %s to index",, name);
  struct file_index * new;
  MAINLOCK;
  if((new = get_fileindex_mutex_free(name,1)) != NULL){
    D("File %s  already exists in index",, name);
    MAINUNLOCK;
    if(status & FILESTATUS_RECORDING)
    {
      if(new->status & FILESTATUS_RECORDING)
      {
	E("Cant give recording status to a file thats already being recorded");
	return NULL;
      }
      else{
	D("Adding status recording to existing file index");
	new->status |= FILESTATUS_RECORDING;
      }
    }
    return new;
  }
  D("File %s really is new",, name);
  if((new=get_last()) == NULL)
  {
    D("First file in index!");
    new = (struct file_index*)malloc(sizeof(struct file_index));
    CHECK_ERR_NONNULL_RN(new);
    files = new;
  }
  else
  {
    D("Not first file in index");
    new->next = (struct file_index*)malloc(sizeof(struct file_index));
    CHECK_ERR_NONNULL_RN(new->next);
    new = new->next;
  }
  memset(new, 0, sizeof(struct file_index));
  /*
  new->augmentlock = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t*));
  CHECK_ERR_NONNULL_RN(new->augmentlock);
  new->waiting = (pthread_cond_t*)malloc(sizeof(pthread_cond_t*));
  CHECK_ERR_NONNULL_RN(new->waiting);
  */
  pthread_rwlock_init(&(new->augmentlock), NULL);
  /* pthread_cond_wait cant wait on rwlock :((( */
  pthread_mutex_init(&(new->wait_mutex), NULL);
  //new->augmentlock = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_init(&(new->waiting), NULL);
  //new->waiting = PTHREAD_COND_INITIALIZER;
  FI_WRITELOCK(new);

  new->filename = (char*)malloc(sizeof(char)*FILENAME_MAX);
  CHECK_ERR_NONNULL_RN(new->filename);
  strncpy(new->filename,name,FILENAME_MAX);
  new->associations = 1;
  MAINUNLOCK;
  /* 0 means its a new recording and we don't know how many we're setting yet */
  if(n_files == 0){
    new->files = 0;
    new->allocated_files = INITIAL_SIZE;
  }
  else
  {
    new->allocated_files = new->n_files = n_files;
  }
  /* +1 because indices start from 0 */
  new->files = (struct fileholder*)malloc(sizeof(struct fileholder)*(new->allocated_files));
  CHECK_ERR_NONNULL_RN(new->files);
  new->status = status;
  if(status == FILESTATUS_RECORDING)
    new->packet_size = packet_size;
  FIUNLOCK(new);
  D("File %s added to index",, new->filename);
  return new;
}
int disassociate(struct file_index* dis, int type)
{
  int err = 0;
  int signal =0;
  if(dis == NULL)
  {
    E("File association is null");
    return -1;
  }
  /*Handling associations requires mainlock */
  FI_WRITELOCK(dis);
  D("Disassociating with %s which has %d associations",, dis->filename, dis->associations);
  assert(dis->associations > 0);
  dis->associations--;
  if(type == FILESTATUS_RECORDING){
    if(!(dis->status & FILESTATUS_RECORDING))
      E("Recorder disassociating eventhough status doesn't show a recorder active!");
    else{
      dis->status &= ~FILESTATUS_RECORDING;
      signal = 1;
    }
  }
  /* Hmm so the other side isn't really relevant ..*/
  if(dis->associations == 0){
    D("%s has no more associations. Closing it",, dis->filename);
    err = close_file_index_mutex_free(dis);
    signal = 0;
  }
  else{
    FIUNLOCK(dis);
    D("Still associations to %s",, dis->filename);
  }
  if(signal == 1)
    wake_up_waiters(dis);
  return err;
}
int mutex_free_wait_on_update(struct file_index *fi)
{
  if(fi->status & FILESTATUS_RECORDING)
    return pthread_cond_wait(&(fi->waiting), &(fi->wait_mutex));
  else {
    E("Asking wait on file thats not being recorded! %s",, fi->filename);
    return -1;
  }
}
int wait_on_update(struct file_index *fi)
{
  int retval;
  FI_CONDLOCK(fi);
  retval = mutex_free_wait_on_update(fi);
  FI_CONDUNLOCK(fi);
  return retval;
}
int mutex_free_wake_up_waiters(struct file_index *fi)
{
  return pthread_cond_broadcast(&(fi->waiting));
}
int wake_up_waiters(struct file_index *fi)
{
  int retval;
  FI_CONDLOCK(fi);
  retval = mutex_free_wake_up_waiters(fi);
  FI_CONDUNLOCK(fi);
  return retval;
}
void and_action(struct file_index* fi, unsigned long filenum, int status, int action)
{
  switch (action)
  {
    case SETFILESTATUS:
      fi->files[filenum].status = status;
      break;
    case ADDTOFILESTATUS:
      fi->files[filenum].status |= status;
      break;
    case DELFROMFILESTATUS:
      fi->files[filenum].status &= ~status;
      break;
    default:
      E("Unknown action");
  }
}
int update_fileholder_status_wname(char * name, unsigned long filenum, int status, int action)
{
  struct file_index* modding =  get_fileindex(name,0);
  if(modding == NULL){
    E("No file with name %s found",, name);
    return -1;
  }
  D("Got filename for update");
  FI_WRITELOCK(modding);
  and_action(modding, filenum, status, action);
  FIUNLOCK(modding);
  return 0;
}
int update_fileholder_status(struct file_index * fi, unsigned long filenum, int status, int action)
{
  FI_WRITELOCK(fi);
  and_action(fi, filenum, status, action);
  FIUNLOCK(fi);
  return 0;
}
int update_fileholder(struct file_index*fi, unsigned long filenum, int status, int action, int recid){
  FI_WRITELOCK(fi);
  and_action(fi, filenum, status, action);
  fi->files[filenum].diskid = recid;
  FIUNLOCK(fi);
  return 0;
}
inline void zero_fileholder(struct fileholder* fh)
{
  memset(fh, 0, sizeof(struct fileholder));
}
int remove_specific_from_fileholders(char* name, int recid)
{
  struct file_index* modding =  get_fileindex(name,0);
  if(modding == NULL){
    E("file index %s not found",, name);
    return -1;
  }
  unsigned long i;
  FI_WRITELOCK(modding);
  for(i=0;i<modding->n_files;i++){
    if(modding->files[i].diskid == recid){
      modding->files[i].diskid = -1;
      modding->files[i].status &= ~FH_ONDISK;
      modding->files[i].status |= FH_MISSING;
    }
  }
  FIUNLOCK(modding);
  return 0;
}
long unsigned get_n_files(struct file_index* fi)
{
  long unsigned ret;
  FI_READLOCK(fi);
  ret = fi->n_files;
  FIUNLOCK(fi);
  return ret;
  //return __sync_fetch_and_add(&(fi->n_files),0);
}
long unsigned get_n_packets(struct file_index* fi)
{
  long unsigned ret;
  FI_READLOCK(fi);
  ret = fi->n_packets;
  FIUNLOCK(fi);
  return ret;
  //return __sync_fetch_and_add(&(fi->n_packets),0);
}
unsigned long add_to_packets(struct file_index *fi, unsigned long n_packets_to_add)
{
  unsigned long n_packets;
  FI_WRITELOCK(fi);
  fi->n_packets+=n_packets_to_add;
  n_packets = fi->n_packets;
  FIUNLOCK(fi);
  return n_packets;
}
int get_status(struct file_index * fi)
{
  int ret;
  FI_READLOCK(fi);
  ret = fi->status;
  FIUNLOCK(fi);
  return ret;
  //return __sync_fetch_and_add(&(fi->status),0);
}
int full_metadata_update(struct file_index* fi, long unsigned * files, long unsigned * packets, int *status)
{
  FI_READLOCK(fi);
  *files = fi->n_files;
  *packets = fi->n_packets;
  *status = fi->status;
  FIUNLOCK(fi);
  return 0;
}
unsigned long get_packet_size(struct file_index* fi)
{
  unsigned long temp;
  FI_READLOCK(fi);
  temp = fi->packet_size;
  FIUNLOCK(fi);
  return temp;
}
int check_for_file_on_disk_and_wait_if_not(struct file_index *fi, unsigned long fileid)
{
  FI_READLOCK(fi);
  while(!(fi->files[fileid].status & FH_ONDISK))
  {
    if(fi->files[fileid].status & FH_MISSING){
      E("Recording %s File %ld gone missing so wont wait for it" ,,fi->filename, fileid);
      FIUNLOCK(fi);
      return -1;
    }
    D("Recording %s is busy with file %ld. Not acquiring write point for it",, fi->filename, fileid);
    FI_CONDLOCK(fi);
    FIUNLOCK(fi);
    mutex_free_wait_on_update(fi);
    FI_CONDUNLOCK(fi);
    FI_READLOCK(fi);
  }
  FIUNLOCK(fi);
  return 0;
}
