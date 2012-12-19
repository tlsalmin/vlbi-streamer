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
int close_file_index(struct file_index* closing)
{
  struct file_index* temp;
  int notfound=0;
  if(closing->associations != 0){
    E("Closing file which still has associations: %s!",, closing->filename);
  }
  if(closing == NULL){
    E("closing is null!");
    return -1;
  }

  MAINLOCK;
  temp = files;
  if(temp == NULL){
    E("No files to close in index!");
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
    if(notfound == 0 && closing->next != NULL)
      temp->next = closing->next;
  }
  MAINUNLOCK;
  free(closing->files);
  pthread_cond_destroy(&(closing->waiting));
  pthread_mutex_destroy(&(closing->augmentlock));
  free(closing->filename);
  free(closing);
  return 0;
}
inline int add_file_mutexfree(struct file_index* fi, long unsigned id, int diskid, int status)
{
  if(id > fi->allocated_files){
    void* tempfiles = fi->files;
    if((fi->files = realloc(fi->files, (fi->allocated_files << 1)*sizeof(struct fileholder))) == NULL){
      E("Realloc failed!");
      fi->files = tempfiles;
      FIUNLOCK(fi);
      return -1;
    }
    fi->allocated_files = fi->allocated_files << 1;
  }
  fi->files[id].diskid = diskid;
  fi->files[id].status = status;
  /*
  fh->diskid = diskid;
  fh_>status = status;
  */
  fi->n_files++;
  return 0;
}
int add_file(struct file_index* fi, long unsigned id, int diskid, int status)
{
  int err;
  FILOCK(fi);
  err = add_file_mutexfree(fi,id,diskid,status);
  FIUNLOCK(fi);
  return err;
}
int close_active_file_index()
{
  //MAINLOCK;
  struct file_index* temp = files;
  struct file_index* temp2;
  while(temp != NULL){
    temp2 = temp;
    temp = temp->next;
    close_file_index(temp2);
  }
  files = NULL;
  //MAINUNLOCK;
  //pthread_spin_destroy(&mainlock);
  CLOSEMAINLOCK;
  //free(mainlock);
  return 0;
}
struct file_index* get_fileindex_mutex_free(char * name, int associate)
{
  struct file_index * temp = files;
  while(files != NULL){
    if(strcmp(files->filename, name) == 0){
      if(associate == 1)
	temp->associations++;
      return temp;
    }
    files = files->next;
  }
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
struct file_index * add_fileindex(char * name, unsigned long n_files, int status)
{
  D("Adding new file %s to index",, name);
  struct file_index * new;
  MAINLOCK;
  if((new = get_fileindex_mutex_free(name,1)) != NULL){
    D("File %s  already exists in index",, name);
    MAINUNLOCK;
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
  pthread_mutex_init(&(new->augmentlock), NULL);
  //new->augmentlock = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_init(&(new->waiting), NULL);
  //new->waiting = PTHREAD_COND_INITIALIZER;
  MAINUNLOCK;
  FILOCK(new);

  new->filename = (char*)malloc(sizeof(char)*FILENAME_MAX);
  CHECK_ERR_NONNULL_RN(new->filename);
  strcpy(new->filename,name);
  /* 0 means its a new recording and we don't know how many we're setting yet */
  if(n_files == 0){
    new->files = 0;
    new->allocated_files = INITIAL_SIZE;
  }
  else
    new->allocated_files = new->n_files = (n_files-1);
  /* +1 because indices start from 0 */
  new->files = (struct fileholder*)malloc(sizeof(struct fileholder)*(new->allocated_files+1));
  CHECK_ERR_NONNULL_RN(new->files);
  new->associations = 1;
  new->status = status;
  FIUNLOCK(new);
  D("File %s added to index",, new->filename);
  return new;
}
int disassociate(struct file_index* dis, int type)
{
  if(dis == NULL)
  {
    E("File association is null");
    return -1;
  }
  MAINLOCK;
  assert(dis->associations > 0);
  dis->associations--;
  if(type == FILESTATUS_RECORDING)
    dis->status &= ~FILESTATUS_RECORDING;
  /* Hmm so the other side isn't really relevant ..*/
  MAINUNLOCK;
  if(dis->associations == 0)
    return close_file_index(dis);
  return 0;
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
  FILOCK(modding);
  and_action(modding, filenum, status, action);
  FIUNLOCK(modding);
  return 0;
}
int update_fileholder_status(struct file_index * fi, unsigned long filenum, int status, int action)
{
  FILOCK(fi);
  and_action(fi, filenum, status, action);
  FIUNLOCK(fi);
  return 0;
}
int update_fileholder(struct file_index*fi, unsigned long filenum, int status, int action, int recid){
  FILOCK(fi);
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
  FILOCK(modding);
  for(i=0;i<modding->n_files;i++){
    if(modding->files[i].diskid == recid){
      modding->files[i].diskid = -1;
      //if(!(modding->files[i].status & FH_INMEM))
      modding->files[i].status |= FH_MISSING;
      //else
      //D("File still in mem so not setting to missing");
    }
  }
  FIUNLOCK(modding);
  return 0;
}
long unsigned get_n_files(struct file_index* fi)
{
  //long unsigned ret;
  //FILOCK(fi);
  //ret = fi->n_files;
  //FIUNLOCK(fi);
  //return ret;
  return __sync_fetch_and_add(&(fi->n_files),0);
}
long unsigned get_n_packets(struct file_index* fi)
{
  /*
  long unsigned ret;
  FILOCK(fi);
  ret = fi->n_packets;
  FIUNLOCK(fi);
  return ret;
  */
  return __sync_fetch_and_add(&(fi->n_packets),0);
}
int get_status(struct file_index * fi)
{
  /*
  int ret;
  MAINLOCK;
  ret = fi->status;
  MAINUNLOCK;
  return ret;
  */
  return __sync_fetch_and_add(&(fi->status),0);
}
