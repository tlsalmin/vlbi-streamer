
/*
 * Mucho gracias http://www.cs.nmsu.edu/~pfeiffer/fuse-tutorial/
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#define FUSE_USE_VERSION 26
#include <fuse.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <libconfig.h>
#include <regex.h> /* For regexp file matching */
#define MAX(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })
#define MIN(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

#define CFGFILE SYSCONFDIR "/vlbistreamer.conf"

#define DEBUG_FROM_REMOTE
#ifdef DEBUG_FROM_REMOTE
#define STATEFILE "/home/kharn/sshfs/watt.vlbi.metsahovi.fi/usr/local/var/opt/vlbistreamer/schedule"
#else
#define STATEFILE LOCALSTATEDIR "/opt/vlbistreamer/schedule"
#endif

#include "../src/configcommon.h"
#include "../src/logging.h"
#define DEBUG_OUTPUT 1
#define B(x) (1l << x)
#define VBS_DATA ((struct vbs_state *) (fuse_get_context()->private_data))

#define STATUS_SLOTFREE		0
#define STATUS_GOTREFSTAT 	B(0)
#define STATUS_INITIALIZED 	B(1)
#define STATUS_OPEN 		B(2)
#define STATUS_NOCFG		B(3)
#define STATUS_NOTVBS		B(4)
#define STATUS_FOUND		B(5)
#define STATUS_CFGPARSED	B(6)

#define RO_STATUS_NOTSTARTED	B(0)
#define RO_STATUS_STARTED	B(1)
#define RO_STATUS_ERROR		B(2)
#define RO_STATUS_NOSUCHFILE	B(3)
#define RO_STATUS_FINISHED_AOK	B(4)
#define RO_STATUS_IDIDNTEVEN	B(5)
#define RO_STATUS_DOREAD	B(6)
#define RO_STATUS_DOWRITE	B(7)

#define INITIAL_FILENUM		1024

#define DISKINDEX_INT 		int8_t
#define DISKINDEX_INITIAL_SIZE 	128

#define MAX_PIPE_SIZE 1048576

#define FILOCK(x) do {D("MUTEXNOTE: Locking file mutex"); pthread_mutex_lock(&(x));} while(0)
#define FIUNLOCK(x) do {D("MUTEXNOTE: Unlocking file mutex"); pthread_mutex_unlock(&((x)));} while(0)

#define SINGLELOCK do {D("MUTEXNOTE: Locking single mutex"); if(pthread_mutex_lock(&(fi->augmentlock))!= 0) E("Error in mutex single lock");} while(0)
#define SINGLEUNLOCK do {D("MUTEXNOTE: Unlocking single mutex"); if(pthread_mutex_unlock(&(fi->augmentlock))!= 0) E("Error in mutex single unlock");} while(0)

#define FORM_VBS_REC_DIR(x) sprintf((x), "%s%d%c%s", vbs_data->rootdir, i, '/', fi->filename)
#define FORM_FILEPATH(x, filenum) sprintf((x), "%s%d/%s/%s.%08ld", vbs_data->rootdir, fi->fileid[(filenum)], fi->filename, fi->filename, (filenum))

#define MAINLOCK vbs_data->augmentlock

#define INDEXING_LENGTH 8
#define MAX_OPEN_FILES_IN_VBS_FS 32

#define FUSE_ARGS_INIT(argc, argv) { argc, argv, 0 }


struct file_index{
  char *filename;
  int status;
  int packet_size;
  unsigned long filesize;
  unsigned long n_packets;
  long unsigned n_files;
  long unsigned files_missing;
  long unsigned allocated_files;
  struct stat refstat;
  pthread_mutex_t  augmentlock;
  DISKINDEX_INT * fileid;
};
struct read_operator{
  struct file_index *fi;
  void * buf;
  char filename[FILENAME_MAX];
  off_t offset;
  size_t size;
  int status;
  struct vbs_state *vbs_data;
};

/*
struct open_file{
  struct file_index * fi;
  int flags;
};
*/

struct vbs_state{
  char* rootdir;
  char* rootofrootdirs;
  char* rootdirextension;
  //char** datadirs;
  int n_datadirs;
  int n_files;
  int offset;
  int max_files;
  //struct open_file* open_files;
  struct file_index* fis;
  pthread_mutex_t  augmentlock;
  int opts;
};

struct vbs_state *vbs_data;

int create_single_file_index(struct file_index *fi, const struct vbs_state *vbs_data); 
int fi_update_from_files(struct file_index *fi, DIR *df, regex_t *regex, unsigned int dirindex, char * dirpath);
int quickcheck_index_for_file(const char * file);
int add_file_to_index(const char * filename, const int index,struct vbs_state * vbs_data);
int quickcheck_n_folders();

void init_vbs_state(struct vbs_state* vbsd)
{
  memset(vbsd, 0,sizeof(struct vbs_state));
}
void vbs_usage()
{
    E("usage:  vbs_fuse [FUSE and mount options] rootdir mountPoint");
    exit(-1);
}
int strip_last(char * original, char * stripped)
{
  char * lrindex;
  memset(stripped, 0, sizeof(char)*FILENAME_MAX);
  lrindex = strrchr(original, '/');
  if(lrindex == NULL){
    E("Cant right strip %s, since rindex is null",, original);
    return -1;
  }
  int diff = (void*)lrindex-(void*)original;
  memcpy(stripped, original, diff);
  return 0;
}
uint64_t vbs_hashfunction(const char* key, struct vbs_state *vbs_data, int get_not_set)
{
  int i;
  int n_loops;
  void * keypointer;
  uint64_t sum=0;
  int value;
  if(strlen(key) ==0){
    D("Empty key");
    return -1;
  }
  /* Loop through each sizeof(int8_t) segment. Defined behaviour	*/
  /* Is floor so no worries on reading over the buffer			*/
  n_loops = ((strlen(key)*sizeof(char)) / (sizeof(uint8_t)));
  if(n_loops == 0){
    D("Zero loops");
    return -1;
  }
  keypointer = (void*)key;
  for(i=0;i<n_loops;i+=sizeof(uint8_t))
  {
    sum += *((uint8_t*)keypointer);
    keypointer+=sizeof(uint8_t);
  }

  value = sum % vbs_data->max_files;
  if(vbs_data->fis[value].status == STATUS_SLOTFREE){
    D("%d Is a free slot!",, value);
    if(get_not_set == 1)
      value =-1;
  }
  else
  {
    int goneover=0;
    while(vbs_data->fis[value].status != STATUS_SLOTFREE && strcmp(vbs_data->fis[value].filename, key) != 0){
      //D("%ld already occupied by %s",,value, vbs_data->fis[value].filename);
      value++;
      if(value >= vbs_data->max_files){
	D("Going over");
	/* Just making sure we wont go looping forever */
	if(goneover == 0){
	  goneover =1;
	}
	else{
	  E("Hash function cant find a free spot!");
	  return -1;
	}
	value=0;
      }
    }
  }
  D("Got %d for %s",, value, key);

  return value;
}
//  All the paths I see are relative to the root of the mounted
//  filesystem.  In order to get to the underlying filesystem, I need to
//  have the mountpoint.  I'll save it away early on in main(), and then
//  whenever I need a path for something I'll call this to construct
//  it.
static void vbs_fullpath(char fpath[PATH_MAX], const char *path)
{
  LOG("rootdir is set as %s", VBS_DATA->rootdir);
    strcpy(fpath, VBS_DATA->rootdir);
    strncat(fpath, path, PATH_MAX); // ridiculously long paths will
				    // break here
    D("vbs_fullpath:  rootdir = \"%s\", path = \"%s\", fpath = \"%s\"",, VBS_DATA->rootdir, path, fpath);
}

int parse_cfg(config_setting_t * root, struct file_index *fi)
{
  int i;
  config_setting_t * setting;
  for(i=0; (setting = config_setting_get_elem(root, i))!= NULL; i++)
  {
    if(strcmp(config_setting_name(setting), "packet_size") == 0)
    {
      CHECK_IS_INT64;
      fi->packet_size = config_setting_get_int64(setting);
      D("Found packet size in cfg");
    }
    CFG_ELIF("cumul")
    {
      CHECK_IS_INT64;
      fi->n_files = config_setting_get_int64(setting);
      D("Found n_files in cfg");
    }
    CFG_ELIF("total_packets")
    {
      CHECK_IS_INT64;
      fi->n_packets = config_setting_get_int64(setting);
      D("Found n_packets in cfg");
    }
  }
  return 0;
}
int get_fi_from_cfg(char * path, struct file_index * fi)
{
  int err;
  int retval=0;

  config_setting_t * root;
  config_t cfg;
  config_init(&cfg);

  err = config_read_file(&cfg, path);
  CHECK_CFG("read cfg");

  root = config_root_setting(&cfg);
  if(root == NULL){
    E("Error in getting root");
    retval = -1;
  }
  else
  {
    err = parse_cfg(root, fi);
    if(err != 0){
      E("Error in parse cfg");
      retval = -1;
    }
  }
  config_destroy(&cfg);

  return retval;
}
int parse_schedule_for_fi(struct file_index *fi)
{
  int i,j;
  int retval=-1;
  int filename_ok = 0;
  int is_rec = 0;
  config_t cfg;
  config_init(&cfg);
  config_read_file(&cfg, STATEFILE);
  config_setting_t *root, *setting, *inner_setting;
  D("Checking schedule file %s for entry",, STATEFILE);

  root = config_root_setting(&cfg);
  if(root == NULL){
    E("Error in getting root");
    config_destroy(&cfg);
    return -1;
  }
  SINGLELOCK;
  /* setting is always the random string to identify the recordings 	*/
  /* So need to use inner_setting to check filename 			*/
  for(i=0; (setting = config_setting_get_elem(root, i))!= NULL; i++)
  {
    for(j=0; (inner_setting = config_setting_get_elem(setting,j)) != NULL ; j++)
    {
      if(strcmp(config_setting_name(inner_setting), "filename") == 0)
      {
	D("Found filename");
	if(strcmp(config_setting_get_string(inner_setting), fi->filename) == 0)
	{
	  D("Found correct %s in schedule",, fi->filename);
	  filename_ok = 1;
	}
	else{
	  break;
	}
      }
      else if(strcmp(config_setting_name(inner_setting), "record")==0)
      {
	if(config_setting_get_int(inner_setting) == 1)
	{
	  D("Is record");
	  is_rec = 1;
	}
      }
    }
    if(is_rec == 1 && filename_ok == 1){
      D("Found %s in schedule!",, fi->filename);
      D("Updating %s from schedule",, fi->filename);
      if(parse_cfg(setting, fi) != 0){
	E("Error in parsecfg");
	retval = -1;
      }
      else
	retval = 0;
      break;
    }
    is_rec =0;
    filename_ok = 0;
  }
  SINGLEUNLOCK;
  config_destroy(&cfg);
  return retval;
}
int get_and_update_from_own_cfg(struct file_index *fi, char *dir)
{
  int err;
  struct stat st_temp;
  char cfgname[FILENAME_MAX];
  sprintf(cfgname, "%s%c%s%s", dir, '/', fi->filename, ".cfg");
  D("Statting cfg %s",, cfgname);
  err = stat(cfgname, &st_temp);

  if(err == -1){
    if(errno != ENOENT)
    {
      E("Not enoent, but worse!");
    }
    D("No cfg file found");
    //fi->status |= STATUS_NOCFG;

    return -1;
  }
  if(!(fi->status & STATUS_GOTREFSTAT))
  {
    memcpy(&fi->refstat, &st_temp, sizeof(struct stat));
    D("Setting refstat for %s",, fi->filename);
    fi->status |= STATUS_GOTREFSTAT;
  }
  //found_cfg =1;
  D("Getting cfg from fi on %s",, cfgname);
  err = get_fi_from_cfg(cfgname, fi);
  if(err != 0){
    E("Error in parsing cfg");
    return -1;
  }
  if(fi->status & STATUS_NOCFG)
  {
    D("A session has finished recording it seems");
    fi->status &= ~STATUS_NOCFG;
  }
  fi->status |= STATUS_CFGPARSED;
  if(fi->fileid == NULL){
    fi->fileid = (DISKINDEX_INT*)malloc(sizeof(DISKINDEX_INT)*fi->n_files);
    memset(fi->fileid, -1, sizeof(DISKINDEX_INT)*fi->n_files);
  }
  fi->allocated_files = fi->n_files;
  D("Parsed cfg %s",, cfgname);

  return 0;
}
int check_all_dirs_for_cfg(const struct file_index * fi, const struct vbs_state *vbs_data)
{
  char temp[FILENAME_MAX];
  int i;
  struct stat st;
  int found=-1;
  for(i=0;i<vbs_data->n_datadirs;i++)
  {
    sprintf(temp, "%s%d%c%s%s", vbs_data->rootdir, i, '/', fi->filename, ".cfg");
    if (stat(temp, &st) != 0)
      D("Not in %s",, temp);
    else{
      found=0;
      break;
    }
  }
  return found;
}
int update_nocfg_index(struct file_index *fi, const struct vbs_state *vbs_data)
{
  int err,i;
  D("Getting preliminary info from cfg-file");

  err = get_fi_from_cfg(CFGFILE, fi);
  if(err != 0){
    E("error in reading cfg %s",, CFGFILE);
    return -1;
  }
  err = parse_schedule_for_fi(fi);
  if(err != 0)
  {
    D("No %s in schedule",, fi->filename);
    if(check_all_dirs_for_cfg(fi, vbs_data) != 0){
      D("%s not found in schedule nor folders. Setting as non-vlbistreamer file",, fi->filename);
      SINGLELOCK;
      fi->status |= STATUS_NOTVBS;
      SINGLEUNLOCK;
      return 0;
    }
    else{
      D("%s seems to have finished",, fi->filename);
      SINGLELOCK;
      fi->status &= ~STATUS_NOCFG;
      SINGLEUNLOCK;
      return create_single_file_index(fi,vbs_data);
    }
  }

  if(fi->fileid == NULL){
    fi->fileid = malloc(sizeof(DISKINDEX_INT)*DISKINDEX_INITIAL_SIZE);
    fi->allocated_files = DISKINDEX_INITIAL_SIZE;
    memset(fi->fileid, -1, sizeof(DISKINDEX_INT)*fi->allocated_files);
  }

  regex_t regex;
  char regstring[FILENAME_MAX];
  sprintf(regstring, "^%s.[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]", fi->filename);
  err = regcomp(&regex, regstring, 0);
  CHECK_ERR("Regcomp");

  for(i=0;i<vbs_data->n_datadirs;i++)
  {
    DIR *df;
    char dirname[FILENAME_MAX];
    FORM_VBS_REC_DIR(dirname);

    df = opendir(dirname);

    if(df ==NULL)
    {
      D("No dir %s",, dirname);
      continue;
    }
    D("Found dir %s",, dirname);
    err = fi_update_from_files(fi, df, &regex, i, dirname);
    CHECK_ERR("update fi_from_files in no_cfg");
  }

  /* TODO: Why regfreeing this segfaults? */
  //regfree(&regex);
  D("Done updating nocfg");
  return 0;
}
int fi_update_from_files(struct file_index *fi, DIR *df, regex_t *regex, unsigned int dirindex, char * dirpath)
{
  struct dirent * de;
  int err;

  while((de = readdir(df)) != NULL)
  {
    err = regexec(regex, de->d_name, 0,NULL,0);
    if(!err)
    {
      D("Regexp matched %s",, de->d_name);
      /* Grab the INDEXING_LENGTH last chars from ent->d_name, which is the	*/
      /* The files index							*/
      char * start_of_index= de->d_name+(strlen(de->d_name))-INDEXING_LENGTH;
      //Hehe why don't I just use start_of_index..
      //memcpy(the_index,start_of_index,INDEXING_LENGTH);
      //temp = atoi(ent->d_name);
      //temp = atoi(the_index);
      int temp = atoi(start_of_index);
      if((unsigned long)temp >= fi->n_files)
      {
	if(fi->status & STATUS_NOCFG && (!(fi->status & STATUS_NOTVBS)))
	{
	  fi->n_files = temp;
	  D("New maximum found for live as %ld",, fi->n_files);
	}
	else
	{
	  E("Extra files found in dir named! Temp read %i, the_index: %s",, temp, start_of_index);
	  continue;
	}
      }
      {
	/* TODO Heres a potential nasty bug if the filesize is of the last file
	 * or its from an unfinished file. This should be fixed on a read
	 * command.	*/
	if(fi->filesize == 0)
	{
	  char tempfilename[FILENAME_MAX];
	  sprintf(tempfilename, "%s%c%s", dirpath, '/', de->d_name);
	  struct stat temp_again;
	  err = stat(tempfilename,  &temp_again);
	  if(err != 0)
	    E("Error in getting stat for %s",, tempfilename);
	  else
	  {
	    D("Adding filesize as %ld for %s",, temp_again.st_size, tempfilename);
	    fi->filesize =temp_again.st_size;
	    if(fi->packet_size != 0)
	      fi->filesize -= fi->filesize % (fi->packet_size-vbs_data->offset);
	    else
	      E("Cant get correct filesize as packet size is not set!");
	    if(!(fi->status & STATUS_GOTREFSTAT)){
	      D("updating refstat for %s",, fi->filename);
	      memcpy(&fi->refstat, &temp_again, sizeof(struct stat));
	      fi->status |= STATUS_GOTREFSTAT;
	    }
	  }
	}
	D("Identified %s as %d",,start_of_index,  temp);
	if(dirindex >= fi->allocated_files){
	  while(dirindex >= fi->allocated_files)
	    fi->allocated_files = fi->allocated_files << 1;
	  if((fi->fileid = realloc(fi->fileid, fi->allocated_files*sizeof(DISKINDEX_INT))) == NULL)
	  {
	    E("Error in realloc for %s",, fi->filename);
	    return -1;
	  }
	}
	if(dirindex >= fi->n_files)
	  fi->n_files = dirindex+1;

	fi->fileid[temp] = (DISKINDEX_INT)dirindex;
      }
    }
    else if( err == REG_NOMATCH ){
      D("Regexp didn't match %s",, de->d_name);
    }
    else{
      char msgbuf[100];
      regerror(err, regex, msgbuf, sizeof(msgbuf));
      E("Regex match failed: %s",, msgbuf);
    }
  }

  return 0;
}
#define CSFI_PREFAIL do { if(df != NULL){err = closedir(df);} if(err != 0){E("Error in closedir");} SINGLEUNLOCK; regfree(&regex); }while(0)

int create_single_file_index(struct file_index *fi, const struct vbs_state *vbs_data)
{
  char temp[FILENAME_MAX];
  //char cfgname[FILENAME_MAX];
  int i,err;
  int found_atleast_one=0;
  //int found_cfg=0;
  DIR *df;
  //struct dirent* de;
  regex_t regex;
  char regstring[FILENAME_MAX];
  sprintf(regstring, "^%s.[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]", fi->filename);
  //err = regcomp(&regex, "^[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]", 0);
  err = regcomp(&regex, regstring, 0);
  CHECK_ERR("Regcomp");

  SINGLELOCK;
  for(i=0;i<vbs_data->n_datadirs;i++)
  {
    FORM_VBS_REC_DIR(temp);
    D("Checking %s",, temp);
    df = opendir(temp);
    if(df ==NULL)
    {
      D("No dir %s",, temp);
      continue;
    }
    D("Found dir %s",, temp);
    found_atleast_one = 1;
    if(!(fi->status & STATUS_CFGPARSED))
    {
      /* If we dont yet know about this cfg */
      /* The folder exists. Check if it has a cfg */
      err = get_and_update_from_own_cfg(fi, temp);
      if(err != 0){
	fi->status |= STATUS_NOCFG;
	CSFI_PREFAIL;
	D("Presuming %s is an active recording",, fi->filename);
	return update_nocfg_index(fi, vbs_data);
      }
    }

    err = fi_update_from_files(fi, df, &regex, i, temp);
    if(err != 0){
      CSFI_PREFAIL;
      return -1;
    }
    err = closedir(df);
    if(err != 0)
      E("Error in closedir of %s",, fi->filename);
    df = NULL;
  }
  df = NULL;
  CSFI_PREFAIL;
  D("Disk loops done for %s",, fi->filename);

  if (found_atleast_one == 0){
    E("%s Not found although in index!",, fi->filename);
    fi->status |= STATUS_NOTVBS;
    return -1;
  }
  else{
    //fi->status &= ~STATUS_NOTINIT;
    fi->status |= STATUS_INITIALIZED;
  }

  return 0;
}
int update_stat(struct file_index * fi, struct stat * statbuf)
{
  (void)fi;
  (void)statbuf;
  if(fi->status & STATUS_GOTREFSTAT)
    memcpy(statbuf, &(fi->refstat), sizeof(struct stat));
  else
    D("Update but no refstat");
  if(fi->n_packets == 0)
  {
    // TODO not thread safe!
    fi->n_packets = (fi->n_files * fi->filesize)/fi->packet_size;
    D("Using a live recording so have to estimate n_packets as %ld from %ld files",, fi->n_packets, fi->n_files);
  }
  statbuf->st_size = fi->n_packets*(fi->packet_size-vbs_data->offset);
  statbuf->st_blocks = statbuf->st_size / 512;

  return 0;
}
/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored.  The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given.
 */
static int vbs_getattr(const char *path, struct stat *statbuf)
{
  LOG("Running getattr\n");
  int retstat = 0;
  int err = 0;
  int index;
  /* If its the root, just display all recordings */
  if (strcmp(path, "/") == 0) {
    statbuf->st_mode = S_IFDIR | 0755;
    statbuf->st_nlink = 2;
  }
  else
  {
    /* Skipping the / from start */
    D("Looking up %s",, path+1);
    index = vbs_hashfunction(path+1, VBS_DATA, 1);
    if(index < 0)
    {
      if(quickcheck_index_for_file(path+1) ==0){
	D("Found non-indexed file in getattr");
	index = vbs_hashfunction(path+1, VBS_DATA, 0);
	err = add_file_to_index(path+1, index, vbs_data);
	CHECK_ERR("add file to indeX");
	/*
	err = create_single_file_index(&(vbs_data->fis[index]), vbs_data);
	CHECK_ERR("Create
	*/
      }
      else
      {
	E("No such file");
	return -ENOENT;
      }
    }
    struct file_index *fi = &(vbs_data->fis[index]);
    D("Found file %s in index",, fi->filename);

    if(!(fi->status & STATUS_INITIALIZED) && !(fi->status & STATUS_NOTVBS)){
      err = create_single_file_index(fi, vbs_data);
      if(err != 0){
	E("Error in update stat");
	return -ENOENT;
      }
    }
    else if (fi->status & STATUS_NOCFG)
    {
      err = update_nocfg_index(fi, vbs_data);
      if(err != 0){
	E("Error in update stat");
	return -ENOENT;
      }
    }
    if(fi->status & STATUS_NOTVBS)
      return -ENOENT;
    D("updating stats for %s",, fi->filename);
    err = update_stat(fi, statbuf);
    retstat = 0;

    /*
       char fpath[PATH_MAX];

       LOG("\nvbs_getattr(path=\"%s\")\n",path);
       vbs_fullpath(fpath, path);

       LOG("\nvbs_getattr(path=\"%s\"), fullpath=\"%s\"\n", path,fpath);
       retstat = lstat(fpath, statbuf);
       if (retstat != 0){
       retstat = -1;
       E("vbs_getattr lstat");
       }
       */
  }

  //log_stat(statbuf);

  return retstat;
}
/** Create a file node
 *
 * There is no create() operation, mknod() will be called for
 * creation of all non-directory, non-symlink nodes.
 */
// shouldn't that comment be "if" there is no.... ?
int vbs_mknod(const char *path, mode_t mode, dev_t dev)
{
  LOG("Running mknod\n");
  int retstat = 0;
  char fpath[PATH_MAX];

  E("\nvbs_mknod(path=\"%s\", mode=0%3o)",,
      path, mode);
  vbs_fullpath(fpath, path);

  // On Linux this could just be 'mknod(path, mode, rdev)' but this
  //  is more portable
  if (S_ISREG(mode)) {
    retstat = open(fpath, O_CREAT | O_EXCL | O_WRONLY, mode);
    if (retstat < 0)
      E("vbs_mknod open");
    else {
      retstat = close(retstat);
      if (retstat < 0)
	E("vbs_mknod close");
    }
  } else
    if (S_ISFIFO(mode)) {
      retstat = mkfifo(fpath, mode);
      if (retstat < 0)
	E("vbs_mknod mkfifo");
    } else {
      retstat = mknod(fpath, mode, dev);
      if (retstat < 0)
	E("vbs_mknod mknod");
    }

  return retstat;
}
/** Create a directory */
int vbs_mkdir(const char *path, mode_t mode)
{
  LOG("Running mkdir\n");
  int retstat = 0;
  char fpath[PATH_MAX];

  D("\nvbs_mkdir(path=\"%s\", mode=0%3o)",,
      path, mode);
  vbs_fullpath(fpath, path);

  retstat = mkdir(fpath, mode);
  if (retstat < 0)
    E("vbs_mkdir mkdir");

  return retstat;
}
/*
   static int vbs_releasedir(const char* path, struct fuse_file_info *fi)
   {
   (void) path;
   (void) fi;
   E("Not yet implemented");
   return 0;
   }
   static int vbs_release(const char *path, struct fuse_file_info * fi)
   {
//Just a stub.  This method is optional and can safely be left unimplemented 
//TODO: free up structs/maps used 

(void) path;
(void) fi;
E("Not yet implemented");
return 0;
}
static int vbs_fsync(const char *path, int isdatasync, struct fuse_file_info* fi)
{
// Just a stub.  This method is optional and can safely be left unimplemented 

(void)fi;
(void) path;
(void) isdatasync;
E("Not yet implemented");
return 0;
}
*/
void * vbs_init(struct fuse_conn_info *conn)
{
  char * temp;
  LOG("Running init\n");
  /* Might use conn at some point */
  (void)conn;
  if(pthread_mutex_init(&(vbs_data->augmentlock), NULL) != 0)
    perror("Mutex init");

  //vbs_data->rootdirextension;
  //vbs_data->rootofrootdirs = (char*)malloc(sizeof(char)*FILENAME_MAX);
  //CHECK_ERR_NONNULL_RN(vbs_data->rootofrootdirs);

  //memset(vbs_data->rootofrootdirs, 0, sizeof(char)*FILENAME_MAX);
  //memset(vbs_data->rootdirextension, 0, sizeof(char)*FILENAME_MAX);

  temp = rindex(vbs_data->rootdir, '/');
  /* Special case when rootdir ends with '/'. It really shouldn't though! */
  if(((size_t)(temp - vbs_data->rootdir)) == strlen(vbs_data->rootdir)-1)
  {
    LOG("rootdir ending with /\n");
  }
  else
  {
    //size_t diff = (temp -  vbs_data->rootdir + 1);
    //size_t diff_end = (strlen(temp));
    //memcpy(vbs_data->rootofrootdirs, vbs_data->rootdir, diff);
    //memcpy(vbs_data->rootdirextension, temp+1, diff);
    vbs_data->rootofrootdirs = strndup(vbs_data->rootdir, strlen(vbs_data->rootdir)-strlen(temp));
    CHECK_ERR_NONNULL_RN(vbs_data->rootofrootdirs);
    vbs_data->rootdirextension = temp+1;
    LOG("Rootdir is %s, extension %s\n", vbs_data->rootofrootdirs, vbs_data->rootdirextension);
  }

  vbs_data->max_files = INITIAL_FILENUM;
  vbs_data->fis = (struct file_index*)malloc(sizeof(struct file_index)*vbs_data->max_files);
  memset(vbs_data->fis, 0, sizeof(struct file_index)*vbs_data->max_files);

  if(quickcheck_n_folders() != 0)
    E("Error in folder quickcheck");

  /*
  vbs_data->open_files = (struct open_file*)malloc(sizeof(struct open_file)*MAX_OPEN_FILES_IN_VBS_FS);
  memset(vbs_data->open_files, 0, sizeof(struct open_file)*MAX_OPEN_FILES_IN_VBS_FS);
  */

  D("init done");
  return (void*)vbs_data;
}
int mutex_free_rm_file_from_index(const int index,struct vbs_state * vbs_data)
{
  struct file_index* fi = &(vbs_data->fis[index]);

  if(pthread_mutex_destroy(&fi->augmentlock) != 0)
    E("Mutex destroy of %s",,fi->filename);
  free(fi->filename);
  fi->status = 0;
  if(fi->fileid != NULL)
    free(fi->fileid);
  //TODO: Rest of the stuff
  //removing file index int etc.

  return 0;
}
int add_file_to_index(const char * filename, const int index,struct vbs_state * vbs_data)
{
  struct file_index* fi = &(vbs_data->fis[index]);

  if(pthread_mutex_init(&(fi->augmentlock), NULL) != 0)
    perror("Mutex init");
  fi->filename = strdup(filename);
  fi->status |= STATUS_FOUND;
  //TODO: Rest of the stuff

  return 0;
}
void vbs_destroy(void * vd)
{
  int err;
  int i;
  struct vbs_state * vbs_data = (struct vbs_state*)vd;
  err = pthread_mutex_destroy(&(vbs_data->augmentlock));
  if(err != 0)
    E("Error in pthread mutex destroy");
  for(i=0;i<vbs_data->max_files;i++)
  {
    err = mutex_free_rm_file_from_index(i, vbs_data);
    if(err != 0)
      E("Error in closing %d",, i);
  }
  free(vbs_data->fis);
  free(vbs_data->rootdir);
}
static int vbs_open(const char *path, struct fuse_file_info * fi)
{
  LOG("Running open\n");
  (void)fi;
  struct vbs_state* vbs_data = VBS_DATA;
  //struct open_file* of;
  //int retval = -EMFILE;
  int64_t index_of_fi;

  index_of_fi = vbs_hashfunction(path+1, vbs_data, 1);
  if(index_of_fi == -1){
    D("No file %s in vbs",, path);
    return -ENOENT;
  }
  if(vbs_data->fis[index_of_fi].status & STATUS_NOTVBS){
    D("File %s not vbs-file",, path);
    return -ENOENT;
  }

  /*
  for(i=0;i<MAX_OPEN_FILES_IN_VBS_FS;i++)
  {
    of = vbs_data->open_files+i;
    if(of == NULL)
    {
      D("Still space to open %s",, path);

      of->fi = &(vbs_data->fis[index_of_fi]);
      of->flags = fi->flags;
      //retval = i;
      break;
    }
  }
  */
  return 0;
}
#define OPERATION_WITH_SPLICE
#define FLAGS_TO_ERROR_AND_RETURN_NULL ro->status &= ~RO_STATUS_STARTED; ro->status |= RO_STATUS_ERROR; return NULL

long convert_from_woffset_to_truelength(const long amount, const struct file_index *fi,const struct vbs_state * vbs_data)
{
  long truelength = 0;
  long remainder = amount % (fi->packet_size-vbs_data->offset);
  //truelength = (amount/(fi->packet_size-vbs_data->offset))*(fi->packet_size+vbs_data->offset);
  truelength = (amount/(fi->packet_size-vbs_data->offset))*(fi->packet_size);
  if(remainder > 0)
    truelength += vbs_data->offset + remainder;
  D("Converted %ld in offset form to %ld in true length",, amount, truelength);
  return truelength;
}
long convert_from_truelength_to_woffset(const long amount,const struct file_index *fi,const struct vbs_state * vbs_data)
{
  return (amount/fi->packet_size)*(fi->packet_size-vbs_data->offset);
  }

void * do_operation(void* opts)
{
  struct read_operator* ro = (struct read_operator*)opts;
  struct vbs_state *vbs_data = ro->vbs_data;
  int err;
  uint64_t count=ro->size;
  uint64_t nondummy_transacted = 0;
  /*
     if(vbs_data->offset > 0)
     {
     count = convert_from_woffset_to_truelength(count, vbs_data, ro->fi);
     }
     */
  int pid;

  int pipes[2];
  pipes[0]=-1;
  pipes[1]=-1;
  err = pipe(pipes);
  if(err != 0){
    FLAGS_TO_ERROR_AND_RETURN_NULL;
  }

#ifdef F_SETPIPE_SZ
  err = fcntl(pipes[1], F_SETPIPE_SZ, MAX_PIPE_SIZE);
  if(err < 0)
    E("Error in setting pipe size");
#endif


  ro->status &= ~RO_STATUS_NOTSTARTED;
  ro->status |= RO_STATUS_STARTED;
  /*Fork here. parent does the memory side and child the file side */
  pid =fork();
  if(pid == 0){
    D("Child pid for %s is alive",, ro->filename);
    int fd=-1, flags, flags2;
    if(ro->status & RO_STATUS_DOREAD){
      flags2 = S_IRUSR;
      flags = O_RDONLY;
    }
    else if (ro->status & RO_STATUS_DOWRITE){
      flags = O_WRONLY;
      flags = S_IWUSR;
    }

    fd = open(ro->filename, flags, flags2);
    if(fd == -1){
      E("Error in opening %s",, ro->filename);
      ro->status &= ~RO_STATUS_STARTED;
      if(errno == ENOENT){
	E("No such file %s",, ro->filename);
	ro->status |= RO_STATUS_NOSUCHFILE;
      }
      else
	ro->status |= RO_STATUS_ERROR;
      _exit(EXIT_FAILURE);
    }
    if(ro->offset != 0){
      D("Seeking to %ld in splicer",, ro->offset);
      if (lseek(fd, ro->offset, SEEK_SET) < 0 ){
	E("Error in seek");
	close(fd);
	close(pipes[1]);
	_exit(EXIT_FAILURE);
      }
    }
    D("Starting splicing");
    while(count > 0)
    {
      err = splice(fd, NULL, pipes[1], NULL, count, SPLICE_F_MOVE|SPLICE_F_MORE);
      if(err < 0 ){
	E("Error in splicing");
	close(pipes[1]);
	close(fd);
	_exit(EXIT_FAILURE);
      }
      if(err == 0)
      {
	D("No more data to read");
	count = 0;
      }
      else{
	count -= err;
      }
    }
    D("Splicer done");
    close(pipes[1]);
    close(fd);
    _exit(EXIT_SUCCESS);
  }
  else
  {
    D("father pid for %s is alive",, ro->filename);
    int retval_forked = 0;
    if(vbs_data->offset > 0){
      struct iovec iov[2];
      size_t from_boundary;
      size_t transacted = 0;
      void* dummyspace = NULL;
      dummyspace = malloc(vbs_data->offset);
      iov[0].iov_base = dummyspace;
      iov[1].iov_base = ro->buf;
      D("Starting offset vmsplice for %ld",, count);
      while(count > 0 )
      {
	  D("Count is %ld",, count);
	from_boundary = ((ro->offset+transacted) % ro->fi->packet_size);

	if(count <= (unsigned)vbs_data->offset){
	  D("No need to read offset bytes %ld to buffer",, from_boundary);
	  count = 0;
	  break;
	}
	/* We will rewind up to a packet boundary so the loop after this can be	*/
	/* more simple								*/
	/* Have to do this in a loop, since writev might return with less 	*/
	/* read than commanded to read						*/
	D("Cleaning up until packet boundary. %ld from boundary",, from_boundary);

	if(from_boundary < (unsigned)vbs_data->offset)
	{
	  iov[0].iov_len = vbs_data->offset - from_boundary;
	  iov[1].iov_len = MIN(count-iov[0].iov_len, (unsigned)(ro->fi->packet_size-vbs_data->offset));
	  D("Doing doublesplice with %ld to dummy and %ld to buffer",, iov[0].iov_len, iov[1].iov_len);
	  err = vmsplice(pipes[0], iov, 2,0);
	}
	else
	{
	  iov[0].iov_len = 0;
	  iov[1].iov_len = MIN(count, (unsigned)(ro->fi->packet_size - from_boundary));
	  D("Doing singlespice with %ld to buffer",, iov[1].iov_len);
	  err = vmsplice(pipes[0], iov+1, 1,0);
	}

	if(err < 0){
	  E("Error in vmsplice");
	  close(pipes[0]);
	  close(pipes[1]);
	  free(dummyspace);
	  waitpid(pid, &retval_forked, 0);
	  FLAGS_TO_ERROR_AND_RETURN_NULL;
	}
	else if(err == 0)
	{
	  D("No more to read and vmsplice");
	  //close(pipes[0]);
	  count = 0;
	  break;
	}
	else
	{
	  count-=err;
	  transacted+=err;
	  iov[1].iov_base += err - iov[0].iov_len;
	  nondummy_transacted+=err-iov[0].iov_len;
	}

      }
      free(dummyspace);
      D("offset vmsplice finished");
    }
    else
    {
      struct iovec iov;
      iov.iov_len = count;
      iov.iov_base = ro->buf;

      D("Starting simple vmsplice. count is %ld",, count);
      while(count > 0)
      {
	err = vmsplice(pipes[0], &iov, 1,0);
	if(err < 0){
	  E("Error in vmsplice");
	  close(pipes[0]);
	  close(pipes[1]);
	  waitpid(pid, &retval_forked, 0);
	  FLAGS_TO_ERROR_AND_RETURN_NULL;
	}
	else if(err == 0)
	{
	  D("No more to read and vmsplice");
	  count = 0;
	}
	else
	{
	  count -= err;
	  iov.iov_base += err;
	  iov.iov_len = count;
	  nondummy_transacted += err;
	  /* Closing here so won't do double close on err = 0 */
	}
      }
      D("Simple vmsplice finished");
    }
    D("Done and freeing up in vmsplice end");
    close(pipes[0]);
    waitpid(pid, &retval_forked, 0);
    if(retval_forked != EXIT_SUCCESS){
      E("Other pid exited without success");
      FLAGS_TO_ERROR_AND_RETURN_NULL;
    }
  }
  ro->status = RO_STATUS_FINISHED_AOK;
  D("Read finished. Non dummy transacted %ld",,nondummy_transacted);
  pthread_exit(NULL);
}
static int vbs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *finfo)
{
  LOG("Running read\n");
  int64_t fi_i;
  (void)finfo;
  off_t filenum,partial_off;
  void* bufstart = buf;
  int retval = size;
  int i, err, n_files_open;
  /*
  pthread_t* pts;
  struct read_operator * ropts;
  */
  struct file_index *fi;
  struct vbs_state * vbs_data = VBS_DATA;
  uint64_t effective_filesize;

  /* Check if in hash 			*/
  fi_i = vbs_hashfunction(path+1, vbs_data, 1);
  if(fi_i < 0){
    E("No such file %s",, path);
    return -EBADF;
  }
  /* Check if its a vbs_recording	*/
  fi = &(vbs_data->fis[fi_i]);
  if(fi->status & STATUS_NOTVBS){
    E("File %s no vbs_data",, path);
    return -EBADF;
  }

  /* Check if we've created the index	*/
  if(fi->filesize == 0){
    D("reading file with 0 filesize!");
    err = create_single_file_index(fi, vbs_data);
    if(err != 0){
      E("Error in creating file index for %s",, path);
      return -1;
    }
    D("created file index for %s",, path);
  }

  /*
     if(vbs_data->offset != 0)
     {
     D("offset nonzero. Setting augmenting size from %ld to..",, size);
     long n_packets_read = size/(fi->packet_size-vbs_data->offset);
     size = n_packets_read * fi->packet_size;
     D(".. %ld bytes",, size);
     }
     */

  off_t size_of_rec = fi->n_packets*(fi->packet_size-vbs_data->offset);
  if(offset >= size_of_rec)
  {
    E("offset over recording length %ld when max is %ld",, offset, size_of_rec);
    return -EINVAL;
  }
  if(offset+(off_t)size >= size_of_rec)
  {
    D("offset+size %ld + %ld is larger that size_of_rec %ld.",, offset, size, size_of_rec);
    size = size_of_rec-offset;
    retval = EOF;
  }


  if(vbs_data->offset > 0) 
    effective_filesize = convert_from_truelength_to_woffset(fi->filesize, fi, vbs_data);
  else
    effective_filesize = fi->filesize;

  filenum = offset/effective_filesize;
  //partial_off = convert_from_woffset_to_truelength(offset % effective_filesize, fi, vbs_data);
  partial_off = offset % effective_filesize;
  n_files_open = size / effective_filesize;
  /* Check if we go over a file boundary */
  if(effective_filesize - partial_off  < size - n_files_open*effective_filesize)
    n_files_open++;
  /* less than filesize => 0 n_files_open, larger => 1 n_files_open, so ++ing this */
  n_files_open++;


  D("For %s start at file %lu, partial offset %lu n_files_open %d count %ld",, path, filenum, partial_off, n_files_open, size);

  /*
     fds = malloc(sizeof(int)*n_files_open);
     CHECK_ERR_NONNULL(fds, "malloc fds");
     memset(fds, 0, sizeof(int)*n_files_open);
     */

  /*
  pts = malloc(sizeof(pthread_t)*n_files_open);
  CHECK_ERR_NONNULL(pts, "pts malloc");
  ropts = malloc(sizeof(struct read_operator)*n_files_open);
  CHECK_ERR_NONNULL(ropts, "ropts malloc");
  memset(ropts, 0, sizeof(struct read_operator)*n_files_open);
  */
  {
    pthread_t pts[n_files_open];
    struct read_operator ropts[n_files_open];
    memset(&ropts, 0, sizeof(struct read_operator)*n_files_open);

    for(i=0;i<n_files_open;i++)
    {
      struct read_operator *ro = &ropts[i];
      if(fi->fileid[filenum+i] == -1){
	D("Missing %ld:th file from %s",, filenum+i, path);
	ro->status = RO_STATUS_IDIDNTEVEN;
      }
      else
      {
	long amount;
	/*
	   struct file_index *fi;
	   void * buf;
	   char filename[FILENAME_MAX];
	   off_t offset;
	   size_t size;
	   int status;
	   struct vbs_state *vbs_data;
	   */
	ro->fi = fi;
	ro->buf = bufstart;
	FORM_FILEPATH(ro->filename, filenum+i);
	if(vbs_data->offset > 0)
	  ro->offset = convert_from_woffset_to_truelength(partial_off,fi,vbs_data);
	else
	  ro->offset = partial_off;
	if(partial_off + size > effective_filesize)
	  amount = effective_filesize - partial_off;
	else
	  amount = size;
	if(vbs_data->offset > 0)
	  amount = convert_from_woffset_to_truelength(amount, fi, vbs_data);
	ro->size = amount;
	ro->vbs_data = vbs_data;

	D("File n. %ld of %s being read for %ld with offset %ld",, filenum+i, fi->filename, amount, ro->offset);

	ro->status = RO_STATUS_NOTSTARTED|RO_STATUS_DOREAD;
	err = pthread_create(&(pts[i]), NULL, &(do_operation), ro);
	if(err != 0){
	  E("Error in pthread create");
	  ro->status = RO_STATUS_IDIDNTEVEN;
	}
	bufstart += amount;
	size -= amount;
	partial_off = 0;
      }
    }
    for(i=0;i<n_files_open;i++)
    {
      if(!((ropts[i]).status & RO_STATUS_IDIDNTEVEN))
      {
	err = pthread_join(pts[i],NULL);
	if( err != 0)
	  E("Error in pthread join");
	if(ropts[i].status != RO_STATUS_FINISHED_AOK)
	{
	  E("Something went wrong with %d thread for file %s",, i, ropts[i].fi->filename);
	  retval = -1;
	}
	else
	  D("%d file finished ok",, i);
      }
      else{
	D("File %d didnt exist so TODO: filler",, i);
      }
    }
  }
  D("Quitting read. Retval is %d",, retval);
  //retval=size;
  return retval;
}
/*
   int add_or_check_in_index(char* filename, struct vbs_state *vbs_data)
   {
   int i;
   int found=0;
   for(i=0;i<vbs_data->n_files;i++)
   {
   if(strcmp(vbs_data->fis[i].filename,filename) == 0)
   {
   D("Found %s in index",, filename);
   found=1;
   vbs_data->fis[i].status |= STATUS_FOUND;
   break; 
   }
   }
   if(found == 0)
   {
   D("New file %s in index",, de->d_name);
   for(i=0;i<vbs_data->n_files;i++)
   {
   }

   }
   }
   */
int quickcheck_index_for_file(const char * file)
{
  int err;
  char diskpoint[FILENAME_MAX];
  int n_drives= 0;
  struct dirent* de;
  DIR *df;
  struct vbs_state * vbs_data = VBS_DATA;
  int drives_to_go = 1;

  while(drives_to_go == 1)
  {
    memset(diskpoint, 0, sizeof(char)*FILENAME_MAX);
    sprintf(diskpoint, "%s%d", vbs_data->rootdir, n_drives);
    df = opendir(diskpoint); 
    if(df ==NULL)
    {
      if(errno == ENOENT){
	D("No more drivers to check");
	drives_to_go = 0;
      }
      else
      {
	perror("Check drivers");
	return -1;
      }
    }
    else
    {
      D("%s is a valid drive",, diskpoint);
      while((de = readdir(df)) != NULL)
      {
	if(strcmp(de->d_name, "..") == 0 || strcmp(de->d_name, ".") == 0)
	  D("skipping dotties");
	else
	{
	  if(strcmp(de->d_name,file) == 0){
	    D("Found %s",, file);
	    return 0;
	  }
	}
      }
      err = closedir(df);
      CHECK_ERR("Close dir");
    }
    n_drives++;
  }

  return -1;
}
int quickcheck_n_folders()
{
  char diskpoint[FILENAME_MAX];
  int n_drives= 0;
  DIR *df;
  struct vbs_state * vbs_data = VBS_DATA;
  int drives_to_go = 1;

  while(drives_to_go == 1)
  {
    memset(diskpoint, 0, sizeof(char)*FILENAME_MAX);
    sprintf(diskpoint, "%s%d", vbs_data->rootdir, n_drives);
    df = opendir(diskpoint); 
    if(df ==NULL)
    {
      if(errno == ENOENT){
	D("No more drivers to check");
	drives_to_go = 0;
      }
      else
      {
	perror("Check drivers");
	return -1;
      }
    }
    else
      n_drives++;
  }
  vbs_data->n_datadirs = n_drives-1;
  return 0;
}

int rebuild_file_index(){
  int err;
  char diskpoint[FILENAME_MAX];
  int n_drives= 0;
  struct dirent* de;
  struct file_index* fi;
  DIR *df;
  struct vbs_state * vbs_data = VBS_DATA;
  int drives_to_go = 1;

  (void)fi;

  FILOCK(MAINLOCK);
  while(drives_to_go == 1)
  {
    memset(diskpoint, 0, sizeof(char)*FILENAME_MAX);
    sprintf(diskpoint, "%s%d", vbs_data->rootdir, n_drives);
    df = opendir(diskpoint); 
    if(df ==NULL)
    {
      if(errno == ENOENT){
	D("No more drivers to check");
	drives_to_go = 0;
      }
      else
      {
	perror("Check drivers");
	FIUNLOCK(MAINLOCK);
	return -1;
      }
    }
    else
    {
      D("%s is a valid drive",, diskpoint);
      while((de = readdir(df)) != NULL)
      {
	if(strcmp(de->d_name, "..") == 0 || strcmp(de->d_name, ".") == 0)
	  D("skipping dotties");
	else
	{
	  err = vbs_hashfunction(de->d_name, vbs_data,0);
	  if(err > 0)
	  {
	    if(vbs_data->fis[err].status == STATUS_SLOTFREE)
	    {
	      D("new file %s",, de->d_name);
	      err = add_file_to_index(de->d_name, err, vbs_data);
	      if(err != 0)
		E("Err in add file to index");
	    }
	    else
	    {
	      D("Found old file %s",, de->d_name);
	    }

	  }
	  //CHECK_ERR("add or check");
	  //TODO: adding
	}
      }
      err = closedir(df);
      CHECK_ERR("Close dir");
    }
    n_drives++;
  }
  FIUNLOCK(MAINLOCK);
  vbs_data->n_datadirs = n_drives-1;

  return 0;
}

static int vbs_readdir(const char *path, void * buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
  LOG("Running readdir\n");
  int err;
  int i;
  (void)offset;
  (void)fi;
  if (strcmp(path, "/") != 0)
    return -ENOENT;

  err = rebuild_file_index();
  if(err != 0){
    E("Error in rebuilding index for %s",, path);
  }

  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);
  for(i=0;i<vbs_data->max_files;i++)
  {
    struct file_index * fi = &(vbs_data->fis[i]);
    if(fi->status != STATUS_SLOTFREE && !(fi->status & STATUS_NOTVBS))
      filler(buf, fi->filename, NULL, 0);
  }

  return 0;
}
struct fuse_operations vbs_oper = {
  .getattr = vbs_getattr,
  .readdir = vbs_readdir,
  .open = vbs_open,
  .read = vbs_read,
  //.readlink = vbs_readlink,
  // no .getdir -- that's deprecated
  //.getdir = NULL,
  //.mknod = vbs_mknod,
  //.mkdir = vbs_mkdir,
  //.unlink = vbs_unlink,
  //.rmdir = vbs_rmdir,
  //.symlink = vbs_symlink,
  //.rename = vbs_rename,
  //.link = vbs_link,
  //.chmod = vbs_chmod,
  //.chown = vbs_chown,
  //.truncate = vbs_truncate,
  //.utime = vbs_utime,
  //.write = vbs_write,
  /** Just a placeholder, don't set */ // huh???
  //.statfs = vbs_statfs,
  //.flush = vbs_flush,
  //.release = vbs_release,
  //.fsync = vbs_fsync,
  //.setxattr = vbs_setxattr,
  //.getxattr = vbs_getxattr,
  //.listxattr = vbs_listxattr,
  //.removexattr = vbs_removexattr,
  //.opendir = vbs_opendir,
  //.releasedir = vbs_releasedir,
  //.fsyncdir = vbs_fsyncdir,
  .init = vbs_init,
  .destroy = vbs_destroy,
  //.access = vbs_access,
  //.create = vbs_create,
  //.ftruncate = vbs_ftruncate,
};
#define MYFS_OPT(t, p, v) { t, offsetof(struct vbs_state, p), v }

static struct fuse_opt vbs_opts[] = {
  MYFS_OPT("-r %s",             rootdir, 0),
  MYFS_OPT("-f %d",             offset, 0),
  FUSE_OPT_END
    /*
       MYFS_OPT("mystring=%s",       mystring, 0),
       MYFS_OPT("mybool",            mybool, 1),
       MYFS_OPT("nomybool",          mybool, 0),
       MYFS_OPT("--mybool=true",     mybool, 1),
       MYFS_OPT("--mybool=false",    mybool, 0),
       */

    /*
       FUSE_OPT_KEY("-V",             KEY_VERSION),
       FUSE_OPT_KEY("--version",      KEY_VERSION),
       FUSE_OPT_KEY("-h",             KEY_HELP),
       FUSE_OPT_KEY("--help",         KEY_HELP),
       */
};

int main (int argc, char ** argv){
  //struct vbs_state* vbs_data;
  int fuse_stat;

  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

  vbs_data = (struct vbs_state*)malloc(sizeof(struct vbs_state));
  if(vbs_data == NULL){
    E("vbs_data malloc");
    exit(-1);
  }
  init_vbs_state(vbs_data);
  /* Just setting it manually */
  //vbs_data->opts |= DEBUG;

  if ((getuid() == 0) || (geteuid() == 0)) {
    fprintf(stderr, "Running vbs_fuse as root opens unnacceptable security holes\n");
    return 1;
  }
  /* Last two are the opts we want */
  /*
     if ((argc < 3) || (argv[argc-2][0] == '-') || (argv[argc-1][0] == '-'))
     vbs_usage();

     vbs_data->rootdir = realpath(argv[argc-2], NULL);
     argv[argc-2] = argv[argc-1];
     argv[argc-1] = NULL;
     argc--;
     */

  LOG("Testing output\n");
  fuse_opt_parse(&args, vbs_data, vbs_opts, NULL);
  LOG("Fuse opt parse done\n");

  LOG("Read rootdir as %s and offset as %d\n", vbs_data->rootdir, vbs_data->offset);

  LOG("about to call fuse_main\n");
  fuse_stat = fuse_main(args.argc, args.argv, &vbs_oper, vbs_data);
  LOG("fuse_main returned %d\n", fuse_stat);

  /* Free or not to free? */
  //free(vbs_data);

  return fuse_stat;

}
