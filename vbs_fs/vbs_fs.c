/*
 * Mucho gracias http://www.cs.nmsu.edu/~pfeiffer/fuse-tutorial/
 */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
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
#include <config.h>

#include "../src/configcommon.h"
#include "../src/logging.h"
#define DEBUG_OUTPUT 1
#define B(x) (1l << x)
#define VBS_DATA ((struct vbs_state *) (fuse_get_context()->private_data))

#define STATUS_SLOTFREE		0
#define STATUS_NOTINIT 		B(0)
#define STATUS_INITIALIZED 	B(1)
#define STATUS_OPEN 		B(2)
#define STATUS_NOCFG		B(3)
#define STATUS_NOTVBS		B(4)
#define STATUS_FOUND		B(5)
#define STATUS_CFGPARSED	B(6)

#define INITIAL_FILENUM		1024

#define FILOCK(x) do {D("MUTEXNOTE: Locking file mutex"); pthread_mutex_lock(&(x));} while(0)
#define FIUNLOCK(x) do {D("MUTEXNOTE: Unlocking file mutex"); pthread_mutex_unlock(&((x)));} while(0)

#define MAINLOCK vbs_data->augmentlock


#define FUSE_ARGS_INIT(argc, argv) { argc, argv, 0 }

struct file_index{
  char *filename;
  int status;
  int packet_size;
  int filesize;
  unsigned long n_packets;
  long unsigned n_files;
  long unsigned files_missing;
  long unsigned allocated_files;
  struct stat* refstat;
  pthread_mutex_t  augmentlock;
  int * fileid;
};

struct vbs_state{
  char* rootdir;
  char* rootofrootdirs;
  char* rootdirextension;
  //char** datadirs;
  int n_datadirs;
  int n_files;
  uint64_t max_files;
  struct file_index* fis;
  pthread_mutex_t  augmentlock;
  int opts;
};

struct vbs_state *vbs_data;

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
  uint64_t value;
  if(strlen(key) ==0){
    D("Empty key");
    return 0;
  }
  /* Loop through each sizeof(int8_t) segment. Defined behaviour	*/
  /* Is floor so no worries on reading over the buffer			*/
  n_loops = ((strlen(key)*sizeof(char)) / (sizeof(uint8_t)));
  if(n_loops == 0){
    D("Zero loops");
    return 0;
  }
  keypointer = (void*)key;
  for(i=0;i<n_loops;i+=sizeof(uint8_t))
  {
    sum += *((uint8_t*)keypointer);
    keypointer+=sizeof(uint8_t);
  }

  value = sum % vbs_data->max_files;
  if(vbs_data->fis[value].status == STATUS_SLOTFREE){
    D("%ld Is a free slot!",, value);
    if(get_not_set == 1)
      value -1;
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
  D("Got %ld for %s",, value, key);

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
  for(i=0; (temp = config_setting_get_elem(setting, i))!= NULL; i++)
  {
    if(strcmp(config_setting_name(setting), "packet_size") == 0)
    {
      CHECK_IS_INT64;
      CFG_GET_INT64(fi->packet_size);
    }
    CFG_ELIF("cumul")
    {
      CHECK_IS_INT64;
      CFG_GET_INT64(fi->max_files);
    }
    CFG_ELIF("total_packets");
    {
      CHECK_IS_INT64;
      CFG_GET_INT64(fi->n_packets);
    }
  }
}
int get_fi_from_cfg(char * path, struct file_index * fi)
{
  int err, i;
  int retval=0;
  config_setting_t * root, setting;
  config_t cfg;
  config_init(&cfg);

  err = config_read_file(&cfg, path);
  CHECK_CFG("read cfg");

  root = config_root_setting(&cfg);
  if(root == NULL){
    E("Error in getting root");
    return -1;
  }
  err = parse_cfg(root, fi);
  if(err != 0){
    E("Error in parse cfg");
    retval = -1;
  }
  config_destroy(&cfg);
  
  return 0;
}
int create_single_file_index(struct file_index *fi, const struct vbs_state *vbs_data)
{
  char temp[FILENAME_MAX];
  char cfgname[FILENAME_MAX];
  int i,err;
  int found_atleast_one=0;
  int found_cfg=0;
  DIR *df;
  struct dirent* de;
  struct stat * st_temp;

  pthread_mutex_lock(&(fi->augmentlock));
  for(i=0;i<vbs_data->n_datadirs;i++)
  {
    sprintf(temp, "%s%d%s%s", vbs_data->rootdir, i, '/', fi->filename);
    D("Checking %s",, temp);
    df = opendir(temp);
    if(df ==NULL)
    {
      D("No dir %s",, temp);
      continue;
    }
    /* If we dont yet know about this cfg */
    /* The folder exists. Check if it has a cfg */
    sprintf(cfgname, "%s%s%s%s", temp, '/' fi->filename, ".cfg");
    err = stat(cfgname, st_temp);

    if(err == -1){
      if(errno != ENOENT)
      {
	E("Not enoent, but worse!");
      }
      D("No cfg file found");
      fi->status |= STATUS_NOCFG;
      err = closedir(temp);
      if(err != 0)
	E("Error in closedir of %s",, temp);

      pthread_mutex_unlock(&(fi->augmentlock));
      return update_single_file_index(fi,vbs_data);
    }
    err = get_fi_from_cfg(cfgname, fi);
    if(err != 0){
      E("Error in parsing cfg");
      pthread_mutex_unlock(&(fi->augmentlock));
      return -1;
    }

    /*
       while((de = readdir(df)) != NULL)
       {
       }
       */


  }
  pthread_mutex_unlock(&(fi->augmentlock));
  if (found_atleast_one == 0)
    E("%s Not found although in index!",, fi->filename);
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
  /* If its the root, just display all recordings */
  if (strcmp(path, "/") == 0) {
    statbuf->st_mode = S_IFDIR | 0755;
    statbuf->st_nlink = 2;
  }
  else
  {
    /* Skipping the / from start */
    D("Looking up %s",, path+1);
    err = vbs_hashfunction(path+1, VBS_DATA, 1);
    if(err < 0)
    {
      E("No such file");
      return -ENOENT;
    }
    struct file_index *fi = &(vbs_data->fis[err]);
    D("Found file %s in index",, fis->filename);

    if(fi->status & STATUS_NOTINIT){
      err = create_single_file_index(fi, vbs_data);
    }
    else if (fi->status & STATUS_NOCFG)
    {
      err = update_single_file_index(fi, vbs_data);
    }
    if(err != 0){
      E("Error in update/create");
      return -1;
    }

    err = update_stat(fi, vbs_data, statbuf);
    if(err != 0){
      E("Error in update stat");
      return -1;
    }
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


  D("init done");
  return (void*)vbs_data;
}
void vbs_destroy(void * vd)
{
  int err;
  struct vbs_state * vbs_data = (struct vbs_state*)vd;
  err = pthread_mutex_destroy(&(vbs_data->augmentlock));
  if(err != 0)
    E("Error in pthread mutex destroy");
  /* TODO: Free everything */
  free(vbs_data->rootdir);
  free(vbs_data->rootdir);
  free(vbs_data->rootdir);
}
static int vbs_open(const char *path, struct fuse_file_info * fi)
{
  LOG("Running open\n");
  char fpath[PATH_MAX];
  (void)fi;
  vbs_fullpath(fpath, path);

  return open(fpath, O_RDONLY, S_IRUSR);
}
static int vbs_read(const char *path, char *buf, size_t size, off_t offset,
    struct fuse_file_info *fi){
  LOG("Running read\n");
  (void)path;
  (void)buf;
  (void)offset;
  (void)fi;
  return size;
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
int mutex_free_rm_file_from_index(const int index,struct vbs_state * vbs_data)
{
  struct file_index* fi = &(vbs_data->fis[index]);

  if(pthread_mutex_destroy(fi->augmentlock) != 0)
    E("Mutex destroy of %s",m fi->filename);
  free(fi->filename);
  fi->status = 0;
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
  fi->status |= STATUS_NOTINIT;
  fi->status |= STATUS_FOUND;
  //TODO: Rest of the stuff

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
      FILOCK(MAINLOCK);
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
      FIUNLOCK(MAINLOCK);
    }
    n_drives++;
  }

  return 0;
}

static int vbs_readdir(const char *path, void * buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
  int err;
  uint64_t i;
  LOG("Running readdir\n");
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
    if(vbs_data->fis[i].status != STATUS_SLOTFREE)
      filler(buf, vbs_data->fis[i].filename, NULL, 0);
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

  LOG("Read rootdir as %s\n", vbs_data->rootdir);

  LOG("about to call fuse_main\n");
  fuse_stat = fuse_main(args.argc, args.argv, &vbs_oper, vbs_data);
  LOG("fuse_main returned %d\n", fuse_stat);

  /* Free or not to free? */
  //free(vbs_data);

  return fuse_stat;

}
