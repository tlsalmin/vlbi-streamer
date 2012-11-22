/*
 * Mucho gracias http://www.cs.nmsu.edu/~pfeiffer/fuse-tutorial/
 */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/xattr.h>

#define LOG(...) fprintf(stdout, __VA_ARGS__)
#define LOGERR(...) fprintf(stderr, __VA_ARGS__)
#define D(str, ...)\
    do { if(DEBUG_OUTPUT) fprintf(stdout,"%s:%d:%s(): " str "\n",__FILE__,__LINE__,__func__ __VA_ARGS__); } while(0)
#define E(str, ...)\
    do { fprintf(stderr,"ERROR: %s:%d:%s(): " str "\n",__FILE__,__LINE__,__func__ __VA_ARGS__ ); } while(0)

#define VBS_DATA ((struct vbs_state *) fuse_get_context()->private_data)

struct vbs_state{
  char* rootdir;
};
 
void vbs_usage()
{
    E("usage:  vbs_fuse [FUSE and mount options] rootDir mountPoint");
    exit(-1);
}
//  All the paths I see are relative to the root of the mounted
//  filesystem.  In order to get to the underlying filesystem, I need to
//  have the mountpoint.  I'll save it away early on in main(), and then
//  whenever I need a path for something I'll call this to construct
//  it.
static void vbs_fullpath(char fpath[PATH_MAX], const char *path)
{
    strcpy(fpath, VBS_DATA->rootdir);
    strncat(fpath, path, PATH_MAX); // ridiculously long paths will
				    // break here

    log_msg("    vbs_fullpath:  rootdir = \"%s\", path = \"%s\", fpath = \"%s\"\n",
	    VBS_DATA->rootdir, path, fpath);
}
/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored.  The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given.
 */
int vbs_getattr(const char *path, struct stat *statbuf)
{
    int retstat = 0;
    char fpath[PATH_MAX];
    
    log_msg("\nvbs_getattr(path=\"%s\", statbuf=0x%08x)\n",
	  path, statbuf);
    vbs_fullpath(fpath, path);
    
    retstat = lstat(fpath, statbuf);
    if (retstat != 0)
	retstat = vbs_error("vbs_getattr lstat");
    
    log_stat(statbuf);
    
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
    int retstat = 0;
    char fpath[PATH_MAX];
    
    log_msg("\nvbs_mknod(path=\"%s\", mode=0%3o, dev=%lld)\n",
	  path, mode, dev);
    vbs_fullpath(fpath, path);
    
    // On Linux this could just be 'mknod(path, mode, rdev)' but this
    //  is more portable
    if (S_ISREG(mode)) {
        retstat = open(fpath, O_CREAT | O_EXCL | O_WRONLY, mode);
	if (retstat < 0)
	    retstat = E("vbs_mknod open");
        else {
            retstat = close(retstat);
	    if (retstat < 0)
		retstat = E("vbs_mknod close");
	}
    } else
	if (S_ISFIFO(mode)) {
	    retstat = mkfifo(fpath, mode);
	    if (retstat < 0)
		retstat = E("vbs_mknod mkfifo");
	} else {
	    retstat = mknod(fpath, mode, dev);
	    if (retstat < 0)
		retstat = E("vbs_mknod mknod");
	}
    
    return retstat;
}
/** Create a directory */
int vbs_mkdir(const char *path, mode_t mode)
{
    int retstat = 0;
    char fpath[PATH_MAX];
    
    log_msg("\nvbs_mkdir(path=\"%s\", mode=0%3o)\n",
	    path, mode);
    vbs_fullpath(fpath, path);
    
    retstat = mkdir(fpath, mode);
    if (retstat < 0)
	retstat = E("vbs_mkdir mkdir");
    
    return retstat;
}
struct fuse_operations vbs_oper = {
  .getattr = vbs_getattr,
  .readlink = vbs_readlink,
  // no .getdir -- that's deprecated
  .getdir = NULL,
  .mknod = vbs_mknod,
  .mkdir = vbs_mkdir,
  .unlink = vbs_unlink,
  .rmdir = vbs_rmdir,
  .symlink = vbs_symlink,
  .rename = vbs_rename,
  .link = vbs_link,
  .chmod = vbs_chmod,
  .chown = vbs_chown,
  .truncate = vbs_truncate,
  .utime = vbs_utime,
  .open = vbs_open,
  .read = vbs_read,
  .write = vbs_write,
  /** Just a placeholder, don't set */ // huh???
  .statfs = vbs_statfs,
  .flush = vbs_flush,
  .release = vbs_release,
  .fsync = vbs_fsync,
  .setxattr = vbs_setxattr,
  .getxattr = vbs_getxattr,
  .listxattr = vbs_listxattr,
  .removexattr = vbs_removexattr,
  .opendir = vbs_opendir,
  .readdir = vbs_readdir,
  .releasedir = vbs_releasedir,
  .fsyncdir = vbs_fsyncdir,
  .init = vbs_init,
  .destroy = vbs_destroy,
  .access = vbs_access,
  .create = vbs_create,
  .ftruncate = vbs_ftruncate,
  .fgetattr = vbs_fgetattr
};

int main (int argc, char ** argv){
  struct vbs_state* vbs_data;

  if ((getuid() == 0) || (geteuid() == 0)) {
    fprintf(stderr, "Running vbs_fuse as root opens unnacceptable security holes\n");
    return 1;
  }

  vbs_data = (struct vbs_state*)malloc(sizeof(struct vbs_state));
  if(vbs_data == NULL){
    E("vbs_data malloc");
    exit(-1);
  }

  LOG("about to call fuse_main\n");
  fuse_stat = fuse_main(argc, argv, &vbs_oper, vbs_data);
  LOG("fuse_main returned %d\n", fuse_stat);

  /* Free or not to free? */
  //free(vbs_data);

  return fuse_stat;
  
}
