#include <assert.h>
#include <stdlib.h>
#include "active_file_index.h"

struct file_index * files;
pthread_spinlock_t * mainlock;

#define MAINLOCK pthread_spin_lock(mainlock)
#define MAINUNLOCK pthread_spin_unlock(mainlock)

int init_active_file_index()
{
  pthread_spin_init(mainlock, PTHREAD_PROCESS_SHARED);
  files = NULL;//(struct file_index*)malloc(sizeof(struct file_index)*INITIAL_SIZE);
  //CHECK_ERR_NONNULL(files, "Files malloc");
  //memset(files,0,sizeof(struct file_index)*INITIAL_SIZE);
  return 0;
}
int close_file_index(struct file_index* closing)
{
  struct file_index* temp;
  if(closing->associations != 0){
    E("Closing file which still has associations: %s!",, closing->filename);
  }
  FILOCK(closing);
  free(closing->files);
  pthread_cond_destroy(closing->waiting);
  pthread_mutex_destroy(closing->augmentlock);
  FIUNLOCK(closing);
  MAINLOCK;
  temp = files;
  if(temp == closing){
    files = closing->next;
  }
  else
  {
    while(temp->next != closing){
      if(temp->next == NULL){
	E("Closing file not found in file index!");
	free(closing->filename);
	free(closing);
	MAINUNLOCK;
	return -1;
      }
      temp = temp->next;
    }
    if(closing->next != NULL)
      temp->next = closing->next;
  }
  MAINUNLOCK
  return 0;
}
int close_active_file_index()
{
  MAINLOCK;
  struct file_index* temp = files;
  struct file_index* temp2;
  while(temp != NULL){
    temp2 = temp;
    temp = temp->next;
    close_file_index(temp2);
  }
  files = NULL;
  MAINUNLOCK;
  pthread_spin_destroy(mainlock);
  return 0;
}
struct file_index* get_fileindex(char * name)
{
  struct file_index * temp;
  MAINLOCK;
  temp = get_fileindex_mutex_free(name);
  MAINUNLOCK;
  return temp;
}
struct file_index* get_fileindex_mutex_free(char * name)
{
  struct file_index * temp = files;
  while(files != NULL){
    if(strcmp(files->filename, name) == 0)
      return temp;
  }
  return NULL;
}
inline struct file_index* get_last()
{
  struct returnable = files;
  if(returnable == NULL)
    return NULL;
  while(returnable->next != NULL)
    returnable = returnable->next;
  return returnable;
}
/* Returns existing file_index if found or creates new if not */
struct file_index * add_fileindex(char * name, int n_files, int status)
{
  D("Adding new file %s to index",, name)
  struct file_index * new;
  MAINLOCK;
  if((new = get_fileindex_mutex_free(name)) != NULL){
    D("File %s  already exists in index");
    new->associations++;
    MAINUNLOCK;
    return new;
  }
  if((new=get_last()) == NULL)
  {
    files = (struct file_index*)malloc(sizeof(struct file_index));
    new = files;
  }
  else
  {
    new->next = (struct file_index*)malloc(sizeof(struct file_index));
    new = new->next;
  }
  memset(new, 0, sizeof(struct file_index));
  pthread_mutex_init(new->augmentlock, NULL);
  pthread_cond_init(new->waiting, NULL);
  FILOCK(new->augmentlock);
  MAINUNLOCK;

  new->filename = (char*)malloc(sizeof(char)*FILENAME_MAX);
  strcpy(new->filename,name);
  /* 0 means its a new recording and we don't know how many we're setting yet */
  if(n_files == 0){
    new->n_files = INITIAL_SIZE;
  }
  else
    new->n_files = n_files;
  new->files = (struct fileholder*)malloc(sizeof(struct fileholder)*new->n_files);
  new->associations = 1;
  new->status = status;
  FIUNLOCK(new->augmentlock);
  return new;
}
int disassociate(struct file_index* dis)
{
  MAINLOCK;
  assert(dis->associations > 0);
  dis->associations--;
  MAINUNLOCK;
  if(dis->associations == 0)
    return close_file_index(dis);
  return 0;
}
void and_action(struct file_index* fi, int filenum, int status, int action)
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
      return -1;
  }
}
int update_fileholder_status_wname(char * name, int filenum, int status, int action)
{
  int err;
  struct file_index* modding =  get_fileindex(name);
  if(modding == NULL){
    E("No file with name %s found", name);
    return -1;
  }
  FILOCK(modding);
  err = and_action(modding, filenum, status, action);
  FIUNLOCK(modding);
  return err;
}
int update_fileholder_status(struct file_index * fi, int filenum, int status, int action)
{
  int err;
  FILOCK(modding);
  err = and_action(fi, filenum, status, action);
  FIUNLOCK(modding);
  return 0;
}
inline void zero_fileholder(struct fileholder* fh)
{
  memset(fh, 0, sizeof(struct fileholder));
}
int remove_specific_from_fileholders(char* name, int recid)
{
  struct file_index* modding get_fileindex(name);
  if(modding == NULL){
    E("file index %s not found",, name);
    return -1;
  }
  int i;
  FILOCK(modding);
  for(i=0;i<modding->n_files;i++){
    if(modding->files[i].diskid == recid){
      modding->files[i].diskid = -1;
      if(!(modding->files[i].status & FH_INMEM))
	modding->files[i].status |= FH_MISSING;
      else
	D("File still in mem so not setting to missing");
    }
  }
  FIUNLOCK(modding);
  return 0;
}
