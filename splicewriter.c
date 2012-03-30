#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>

#include "streamer.h"
#include "common_wrt.h"
#include "splicewriter.h"



//#define PIPE_STUFF_IN_WRITELOOP
//#define IOVEC_SPLIT_TO_IOV_MAX
/* Default max in 3.2.12. Larger possible if CAP_SYS_RESOURCE */
#define MAX_PIPE_SIZE 1048576
//#define MAX_IOVEC 16

//#define DISABLE_WRITE

struct splice_ops{
  struct iovec* iov;
  int pipes[2];
  int max_pipe_length;
  int pagesize;
};
/*
int splice_all(int from, int to, long long bytes){
  long long bytes_remaining;
  long result;

  bytes_remaining = bytes;
  while (bytes_remaining > 0) {
    result = splice(
	from, NULL,
	to, NULL,
	bytes_remaining,
	SPLICE_F_MOVE | SPLICE_F_MORE
	);

    if (result == -1)
      break;

    bytes_remaining -= result;
  }
  return result;
}
*/
int init_splice(struct opt_s *opts, struct recording_entity * re){
  int ret = common_w_init(opts, re);
  int maxbytes_inpipe;
  if(ret < 0)
    return -1;
  struct common_io_info * ioi = (struct common_io_info*)re->opt;
  struct splice_ops * sp = (struct splice_ops*)malloc(sizeof(struct splice_ops));

#ifndef PIPE_STUFF_IN_WRITELOOP
  ret = pipe(sp->pipes);
  if(ret<0)
    return -1;
#endif
  /* TODO: Handle error for pipe size change */
#ifdef F_SETPIPE_SZ
  fcntl(sp->pipes[1], F_SETPIPE_SZ, MAX_PIPE_SIZE);
  maxbytes_inpipe = fcntl(sp->pipes[1], F_GETPIPE_SZ);
#ifdef DEBUG_OUTPUT
  fprintf(stdout, "SPLICEWRITER: Maximum pipe size set to %d\n", maxbytes_inpipe);
#endif
#else
  /* Old headers so can't query the size. presume its 64KB */
  maxbytes_inpipe = 65536;
#endif

  sp->pagesize = sysconf(_SC_PAGE_SIZE);
  /* Ok lets try to align with physical page size */
  //sp->pagesize = 65536;
  sp->max_pipe_length = maxbytes_inpipe / sp->pagesize;

#ifdef DEBUG_OUTPUT
  fprintf(stdout, "SPLICEWRITER: Max pipe size is %d pages with pagesize %d and buffer bytemax %d\n", sp->max_pipe_length, sp->pagesize, maxbytes_inpipe);
#endif
#ifndef IOVEC_SPLIT_TO_IOV_MAX
  sp->iov =(struct iovec*)malloc(sizeof(struct iovec));
#else
  sp->iov = (struct iovec*)malloc(sp->max_pipe_length * sizeof(struct iovec));
#endif



  ioi->extra_param = (void*)sp;

  return 1;
}

long splice_write(struct recording_entity * re, void * start, size_t count){
  long ret = 0;
#ifdef IOVEC_SPLIT_TO_IOV_MAX
  void * point_to_start=start;
  size_t trycount;
#endif
  //int err = 0;
  int i=0;
  off_t oldoffset;
  //int nr_segs;
  long total_w =0;
#ifdef DEBUG_OUTPUT 
  fprintf(stdout, "SPLICEWRITER: Issuing write of %lu\n", count);
#endif
  struct common_io_info * ioi = (struct common_io_info*) re->opt;
  struct splice_ops *sp = (struct splice_ops *)ioi->extra_param;
#ifndef IOVEC_SPLIT_TO_IOV_MAX
  sp->iov->iov_base = start;
  sp->iov->iov_len = count;
#else
  struct iovec * iov;
#endif

  /* Get current file offset. Used for writeback */
  oldoffset = lseek(ioi->fd, 0, SEEK_CUR);
  ret = count;
  /* TODO: Bidirectional
  if(ioi->read)
    ret = splice_all(ioi->fd, start, count);
  else
    ret = splice_all(start, ioi->fd, count);
    */

#ifdef DISABLE_WRITE
  total_w = count;
#else 
  while(count >0){
#ifdef IOVEC_SPLIT_TO_IOV_MAX
    point_to_start = start;
    trycount = count;
    iov  = sp->iov;
    i=0;
    /* Split the request into page aligned chunks 	*/
    /* TODO: find out where the max size is defined.	*/
    /* Currently splice can handle 16*pagesize 		*/
    while(trycount>0 && i < sp->max_pipe_length && i < IOV_MAX){
      iov->iov_base = point_to_start; 
      /* TODO: Might need to set this only in init 	*/
      iov->iov_len = sp->pagesize;
      point_to_start += sp->pagesize;
      iov += 1;
      trycount-=sp->pagesize;
      /*  Check that we don't go negative on the count */
      /* TODO: This will break if buffer entities aren't pagesize aligned */
      if(trycount>=0)
	i++;
    }
#ifdef DEBUG_OUTPUT
    fprintf(stdout, "SPLICEWRITER: Prepared iovec of %i elements for %i bytes\n", i, i*(sp->pagesize));
#endif
#else
    i=1;
#endif  /* IOVEC_SPLIT_TO_IOV_MAX */

    /* NOTE: SPLICE_F_MORE not *yet* used on vmsplice */
      ret = vmsplice(sp->pipes[1], sp->iov, i, SPLICE_F_GIFT|SPLICE_F_MORE);

      if(ret <0){
	fprintf(stderr, "SPLICEWRITER: vmsplice failed for %i elements and %d bytes\n", i, i*(sp->pagesize));
	break;
      }
      /*
#ifdef DEBUG_OUTPUT 
#ifdef IOVEC_SPLIT_TO_IOV_MAX
      else
	fprintf(stdout, "Vmsplice accepted %i bytes when given %i bytes\n", ret, i*(sp->pagesize));
#endif
#endif
*/

      ret = splice(sp->pipes[0], NULL, ioi->fd, NULL, ret, SPLICE_F_MOVE|SPLICE_F_MORE);

      if(ret<0){
	fprintf(stderr, "SPLICEWRITER: Splice failed for %ld bytes\n", ret);
	break;
      }
      start += ret;
      count -= ret;
#ifndef IOVEC_SPLIT_TO_IOV_MAX
      sp->iov->iov_base += ret;
      sp->iov->iov_len = count;
#endif

      // Update statistics 
      total_w += ret;
  }
  if(ret <0){
    perror("SPLICEWRITER: Error on write/read");
    fprintf(stderr, "SPLICEWRITER: Error happened on %s with start %lu and count %lu\n", ioi->filename, (long)start, count);
    return total_w;
  }
  /* Having bad performance. Linus recommends this stuff  at 			*/
  /* http://lkml.indiana.edu/hypermail/linux/kernel/1005.2/01845.html 		*/
  /* and http://lkml.indiana.edu/hypermail/linux/kernel/1005.2/01953.html 	*/

  /* WEIRD: When I dont call sync_file_range and posix_fadvise the log shows 	*/
  /* that the receive buffers don't go to full. Speed is low at about 3Gb/s 	*/
  /* When both are called, speed goes to 5Gb/s and buffer fulls are logged	*/

  /*
  ret = sync_file_range(ioi->fd,oldoffset,total_w, SYNC_FILE_RANGE_WAIT_BEFORE|SYNC_FILE_RANGE_WRITE|SYNC_FILE_RANGE_WAIT_AFTER);  
  if(ret>=0){
#ifdef DEBUG_OUTPUT 
  fprintf(stdout, "SPLICEWRITER: Write done for %lu in %d loops\n", total_w, i);
#endif
  }
  else{
    perror("splice sync");
    fprintf(stderr, "Sync file range failed on %s, with err %s\n", ioi->filename, strerror(ret));
    return ret;
  }
  ret = posix_fadvise(ioi->fd, oldoffset, total_w, POSIX_FADV_NOREUSE|POSIX_FADV_DONTNEED);
  */
#endif /* DISABLE_WRITE */

  ioi->bytes_exchanged += total_w;
  return total_w;
}
int splice_get_w_fflags(){
  return O_WRONLY|O_DIRECT|O_NOATIME;
  }
int splice_get_r_fflags(){
  return O_RDONLY|O_DIRECT|O_NOATIME;
  }
int splice_close(struct recording_entity *re, void *stats){
  struct common_io_info * ioi = (struct common_io_info*)re->opt;
  struct splice_ops * sp = (struct splice_ops*)ioi->extra_param;
  close(sp->pipes[0]);
  close(sp->pipes[1]);
  free(sp->iov);
  free(ioi->extra_param);
  return common_close(re,stats);
}

int splice_init_splice(struct opt_s *opt, struct recording_entity *re){
  re->init = init_splice;
  re->close = splice_close;
  re->write_index_data = common_write_index_data;

  re->write = splice_write;
  
  re->get_n_packets = common_nofpacks;
  re->get_packet_index = common_pindex;

  re->get_filename = common_wrt_get_filename;

  re->get_r_flags = splice_get_r_fflags;
  re->get_w_flags = splice_get_w_fflags;

  return re->init(opt,re);
}
