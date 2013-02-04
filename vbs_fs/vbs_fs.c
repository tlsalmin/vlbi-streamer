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

#define B(x) (1l << x)
#define DEBUG B(0)
#define IS_DEBUG (((struct vbs_state *) fuse_get_context()->private_data)->opts & DEBUG)
#define LOG(...) fprintf(stdout, __VA_ARGS__)
#define LOGERR(...) fprintf(stderr, __VA_ARGS__)
#define D(str, ...)\
    do { if(IS_DEBUG) fprintf(stdout,"%s:%d:%s(): " str "\n",__FILE__,__LINE__,__func__ __VA_ARGS__); } while(0)
#define E(str, ...)\
    do { fprintf(stderr,"ERROR: %s:%d:%s(): " str "\n",__FILE__,__LINE__,__func__ __VA_ARGS__ ); } while(0)

#define VBS_DATA ((struct vbs_state *) (fuse_get_context()->private_data))


#define STATUS_NOTINIT 		B(0)
#define STATUS_INITIALIZED 	B(1)
#define STATUS_OPEN 		B(2)
#define STATUS_NOCFG		B(3)
#define STATUS_NOTVBS		B(4)


#define FUSE_ARGS_INIT(argc, argv) { argc, argv, 0 }

struct file_index{
  char *filename;
  int status;
  int packet_size;
  int filesize;
  int n_files;
  int * fileid;
};

struct vbs_state{
  char* rootdir;
  char** datadirs;
  struct file_index* head;
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
    E("usage:  vbs_fuse [FUSE and mount options] rootDir mountPoint");
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
  /* If its the root, just display all recordings */
  if (strcmp(path, "/") == 0) {
    statbuf->st_mode = S_IFDIR | 0755;
    statbuf->st_nlink = 2;
  }
  else
  {
    char fpath[PATH_MAX];

    LOG("\nvbs_getattr(path=\"%s\")\n",path);
    vbs_fullpath(fpath, path);

    LOG("\nvbs_getattr(path=\"%s\"), fullpath=\"%s\"\n", path,fpath);
    retstat = lstat(fpath, statbuf);
    if (retstat != 0){
      retstat = -1;
      E("vbs_getattr lstat");
    }
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
  LOG("Running init\n");
  /* Might use conn at some point */
  (void)conn;
  if(pthread_mutex_init(&(vbs_data->augmentlock), NULL) != 0)
    perror("Mutex init");
  return (void*)vbs_data;
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

static int vbs_readdir(const char *path, void * buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
  LOG("Running readdir\n");
  (void)offset;
  (void)fi;
  if (strcmp(path, "/") != 0)
    return -ENOENT;

  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);
  filler(buf, "test", NULL, 0);

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
  //.destroy = vbs_destroy,
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
  vbs_data->opts |= DEBUG;

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
