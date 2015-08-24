#ifndef ACTIVE_FILE_INDEX_H
#define ACTIVE_FILE_INDEX_H

/* For keeping track of files in use */
#include <stdbool.h>
#include <stdio.h>

/**
  Flag-like status for the whole file index.
*/
typedef enum
{
  AFI_RECORD = 1, ///!< A thread is recording this file.
  AFI_SEND = 1 << 1 ///!< A thread is sending this file.
} afi_file_status;

/**
  Exclusive status for file
*/
typedef enum
{
  FH_ONDISK = 1, ///!< File is loadable from disk.
  FH_MISSING = 2, ///!< File is missing from disk and memory
  FH_INMEM = 3, ///!< File is loaded into memory.
  FH_BUSY = 4 ///!< File is busy being recorded.
} afi_fh_status;

struct file_index;

/**
  Initialize active file index. Should be called only ones at the start
  of the program

  @return 0     Success.
  @return != 0  Error.
*/
int afi_init();

/**
  Create a file index with \a name.

  @param        name            Name identifying file.
  @param        n_files         Number of files in file index, if known.
  @param        status          Recording or sending.
  @param        packet_size     Size of each packet in file index.

  @return != NULL       Created file index.
  @return NULL          Error.
*/
struct file_index *afi_add(const char *name, unsigned long n_files,
                           afi_file_status status, unsigned long packet_size);

/**
  Marks all files with given \a name and \a recid as missing (Due to e.g.
  hardware failure

  @param        name    Name of file index.
  @param        id      Disk id upon match the file is set as missing.

  @return       0       Found and updated.
  @return       != 0    file index not found.
*/
int afi_mark_recid_missing(char *name, int id);

/**
  Close the whole file index with all files.

  @return 0     Success.
  @return != 0  Some failures.
*/
int afi_close();

/**
  Get the file index by \a name

  @param name          Name of file index to find.
  @param associate     If true, add association to file.

  @return != NULL      Found file index.
  @return NULL         Couldn't find file index
  */
struct file_index *afi_get(const char *name, bool associate);

/**
  Disassociate with \a fi.

  @param        fi     File index to disassociate with.
  @param        type    If RECORD, all senders will be notified.

  @return       0       Close succeeded or associations remained
  @return       != 0    No associations remained, but close failed.
*/
int afi_disassociate(struct file_index *fi, afi_file_status type);

typedef enum
{
  AFI_ADD_TO_STATUS, ///!< Add given flags from file status.
  AFI_DEL_FROM_STATUS ///!< Remove given flags from file status.
} afi_update_action;

/**
  Update filehandle identified by \a fi and \a filenum

  @param        fi      File index to modify.
  @param        filenum Index of file to modify.
  @param        status  Flags to add / remove.
  @param        action  \ref afi_update_action.
*/
void afi_update_fh(struct file_index *fi, unsigned long filenum, int status,
                   afi_update_action action);

/**
  Returns the number of files associated with \a fi

  @param fi     File index to query

  @return       >= 0
*/
long unsigned afi_get_n_files(struct file_index *fi);

/**
  Returns the number of packets associated with \a fi

  @param fi     File index to query

  @return       >= 0
*/
long unsigned afi_get_n_packets(struct file_index *fi);

/**
  Add a file to \a fi.

  @param        fi      File index file is associated with.
  @param        id      Id of file.
  @param        diskid  Id of disk file resides on.
  @param        status  Status of file

  @return       0       Success.
  @return       != 0    Failure.
*/
int afi_add_file(struct file_index *fi, long unsigned id, int diskid,
                 int status);

/**
  Get status of \a fi

  @param        fi      File index to query.

  @return       \ref afi_file_status
*/
afi_file_status afi_get_status(struct file_index *fi);

/**
  Add newly received packets to a file index.

  @param        fi      File index to modify.
  @param        n_packets_to_add         How many packets to add to file index.

  @return       Number of packets after add.
*/
unsigned long afi_add_to_packets(struct file_index *fi,
                                 unsigned long n_packets_to_add);
/**
  Wait for any updates on file index. e.g. a new file has been recorded.

  @param        fi      File index to wait on.

  @return       0       Success.
  @return       != 0    Failure.
*/
int afi_wait_on_update(struct file_index *fi);

/**
  Wake up any threads waiting for updates for \a fi.

  @param        fi      File index to wake.

  @return       0       Success.
  @return       != 0    Failure.
*/
int afi_wake_up(struct file_index *fi);

/**
  Update given metadata values from \a fi current state

  @param        fi      File index to update from.
  @param        files   Number of files.
  @param        packets Number of packets.
  @param        status  status now.
*/
void afi_update_metadata(struct file_index *fi, long unsigned *files,
                        long unsigned *packets, int *status);

/**
  Get the packet size for \a fi.

  @param        fi      File index to query.

  @return       Packet size associated with file index.
*/
unsigned long afi_get_packet_size(struct file_index *fi);

/**
  Wait for an update on \a fi for file \a fileid

  @param        fi      File index to query.
  @param        fileid  Index of file to wait.

  @return       0       Success.
  @return       != 0    Failure.
*/
int afi_wait_on_file(struct file_index *fi, unsigned long fileid);

/**
  In case the file index is a single file, set all of the files to be found and
  to have the same disk_id

  @param fi             File index to modify.
  @param disk_id        Disk id for every file.
*/
void afi_single_all_found(struct file_index *fi, size_t disk_id);

/**
  Set the status of the \a file in \a fi as found and set it to reside
  on \a disk_id

  @param fi             File index to modify.
  @param file           The index of the file.
  @param disk_id        The index of the disk it resides in.
*/
void afi_set_found(struct file_index *fi, size_t file, size_t disk_id);

/**
  Gets the disk_id for the file identified by \a file from \a fi

  @param fi             File index to query.
  @param file           Index of file.

  @return               Disk id for \a file
*/
size_t afi_get_disk_id(struct file_index *fi, size_t file);

/**
  Returns the filename for \a fi.

  @param fi             File index to query.

  @return               File name for \a fi.
*/
const char *afi_get_filename(struct file_index *fi);

/**
  Initializes all files as missing in \a fi.

  @param fi     File index to modify
*/
void afi_set_all_missing(struct file_index *fi);

/**
  Returns true if file identified by \a file_id in \a fi is missing.

  @param fi             File index to query.
  @param file_id        Index of file to query.

  @return true          File is missing.
  @return false         File is found.
*/
bool afi_is_missing(struct file_index *fi, size_t file_id);

/**
  Start loading files for \a fi.

  TODO: Doc proper.
*/
int afi_start_loading(struct file_index *fi, size_t file_id, bool in_loading);
#endif
