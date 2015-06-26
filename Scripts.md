## Introduction ##

The software has many scripts for helping with its usage, quick deployment and quick recovery from reboot

TODO: Removal of active recordings/streams

## vbs\_record vbs\_send and vbs\_queue ##
```
~/src/vlbi-streamer $ scripts/vbs_queue 
2013-01-28 13:55:34+02:00 Record name or destination/duration missing
OPTIONS:
      -a MYY		Wait MYY microseconds between packet sends
      -c CFGFILE	Load config from cfg-file CFGFILE
      -i INTERFACE	Which interface to bind to(Not required)(Requires root)
      -f OFFSET		Strip offset amount of bytes from header of each packet
      -l LAST_PACKET	number of last packet to be received
      -n IP		Simultaneously send stream to IP
      -p SIZE		Expect SIZE size payload on packets. Reject everything else.
      -q DATATYPE	Receive DATATYPE type of data and resequence (DATATYPE: vdif, mark5b,udpmon,mark5bnet)
      -r RATE		Expected network rate in MB(default: 10000)(Deprecated)
      -s SOCKET	Socket number(Default: 2222)
      -e POSIX_TIME	start event at START_TIME (time since epoch or anything the date command accepts)
      -w		Wait for START_TIME in metadata before starting receive (End time will be moved accordingly)
      -v 		Verbose. Print stats on all transfers
      -y 		Force socket reaqcuire on error (Useful when recordings overlap)
```

vbs\_record and vbs\_send are just wrappers for vbs\_queue.
The main parameters for receiving are: <The name of the capture> <Time in seconds to capture>

Same for sending: <The name of capture to send> <Target IP>


---


```
-i INTERFACE	Which interface to bind to(Not required)
```

bind to specific interface.(Required on fanout & rx-ring). Requires root access and is not recommended.


---

```
-t {fanout|udpstream|sendfile|TODO	Capture type(Default: udpstream)(sendfile is a prototype not yet in kernel)(fanout doesn't write to disk. Poor performance)
```

Only one really working atm. is udpstream, which simply uses a upd-port.

In the near future there might be added to the kernel the possibility of splicing from the socket to the hard-drive, which would enable zero copy writing. sendfile is a prototype for this functionality.

Fanout is a raw-packet capture interface that spreads the packets on to N-threads. It's development was discontinued, as it gave a very high rate of interrupts, probably due to not using interrupt mitigation as efficiently.


---

```
-s SOCKET	Socket number(Default: 2222)
```

---

```
-m {s|r}		Send or Receive the data(Default: receive)
```

Receive new data or send previously recorded data.


---

```
-p SIZE		Set expected packet size
```

vlbistreamer will stop the session if it received > 10 wrong size packets.

---


```
-a MYY		Wait MYY microseconds between packet sends
```

Ensures that packets have atleast a MYY microseconds gap. Note that because we're reading packets from harddrives, that the gap can grow a lot larger sometimes.

```
How to calculate Mb/s rate: ((10^6/MYY)*PACKET_SIZE(Bytes)*8)/(1024*1024) = RATE(Mb/s)
			    MYY = ((10^6)*PACKET_SIZE*8)/(RATE*1024*1024)
```

So if you know you'll be sending 5565 byte packets and you want them sent at 4000Mb/s, MYY would be:

```
((10^6)*5565*8)/(1024*1024*4000) = 10.61439514160156250000
```


---

```
-w {aio|def|splice|dummy|writev}	Choose writer to use(Default: def)
```

aio: Uses libaio(if missing, libaio wasn't found on the machine) as an asynchronous writer. This is currently the most cpu-nonintense write-style.
TODO: adopt aio usage into default writer.

def: Default write-end, with just write/read calls. Good for benchmarking the others.

splice: The most promising writer end, but is still showing relatively poor performance. It uses vmsplice and splice to pass memory references through a pipe, which are then synced to disk.
NOTE: Now uses calls recommended by Linus to the use.TODO: Dig into kernel functions to figure out the best usage.

dummy: Doesn't write anything to disk. Good for checking network read speed/overall performance of receiving end.

writev: Uses a vector write. NOTE: This is the only one currently capable of stripping a starting offset of bits before each packet live.

---

```
-v 		Verbose. Print stats on all transfers
```

Prints the amount send/receive have completed transfer during the last second. Also shows dropped and incomplete packages

---

```
-q DATATYPE	Receive DATATYPE type of data and resequence (DATATYPE: vdif, mark5b,udpmon,mark5bnet)
```
Use datatype for resequencing the packets live. UPDmon adds a 64-bit sequence number to its streams. Utilities for checking and testing this function are described in [Scripts#Sequence\_utilities](Scripts#Sequence_utilities.md)

## Monitoring ploss ##

Since the kernel receive buffers overflows aren't easily queried, there is a script scripts/ploss\_monitor which greps the values from /proc/`<pid>`/net/udp. The value is cumulative.

## Listing recordings ##

vbs\_ls will list all recordings in the ROOTDIRS. Single outside files will show up as recordings

## Removing recordings ##

vbs\_rm removes an existing recording from all drives. Works with wildcards so be careful!

## counter\_secofday ##

Silly scripts for converting local utc-time to second of day and ticking away. Useful for checking if flexbuf is in sync with fila10G. Ofc. resolution is min +-1 second.

## vbs\_clearschedule ##

Empties the schedule, which also cancels all recordings/sendings. Handy for getting a clean slate start after errors.

## Unionfs ##

mount\_unionfs will mount a union fs of the ROOTDIRS for easier access

## vbs\_disk2file ##

```
  Usage scripts/vbs_disk2file <RECNAME> <OUTPUTFILE> <OPTIONS>
OPTIONS:
      -e POSIX_TIME	start event at START_TIME (time since epoch or anything the date command accepts)
      -f OFFSET		Strip offset amount of bytes from header of each packet
```

Will concatenate whole recording to OUTPUTFILE using vlbistreamer daemon.

## nfilepreview ##

Will seek and pipe out n files of a given recording eg.
```
  oper@watt:~/src/vlbi-streamer$ nfilepreview testrecording2 -x 2 | nc -u 192.168.0.1 3232
```
This will concatenate the 2 first files of testrecording2 over netcat to the host 192.168.0.1 port 3232

## Checking sequences ##

To check up on files proper sequences theres a program check\_64\_seq, which grabs the first index and checks if the packets are in sequence after this. If you trust the first index, use the flag -t as last. Otherwise the checker will jump to the next read sequence number and continue checking from there. If you for example made packets with udpmon of size 8888, the bytespacing is 8888.

```
  Usage: seqsrc/check_64_seq <filename> <bytespacing> < -t if you want to trust that the first index is correct>
```

Theres a script scripts/check\_seq\_of\_rec that does this to every file in the recording it is given. It will check the spacing from the cfg-file and check\_64\_seq for every file in the recording.
```
  check_seq_of_rec <recording name>
```

For testing purposes there's also scramble\_64\_seq and scramble2\_64\_seq which scramble packages in between two files.

NOTE: only implemented for udpmon headers. TODO: Implement for VDIF

## Parsing logs ##
path: scripts/parse\_logs
> scripts/plot\_parsed
> scripts/statlogger

After you've logged a session with for example running src/vlbistreamer > the\_log, you can parse the log to a more plottable format by running scripts/parse\_logs `<logfile>`. This will generate a `<logfile>`.parsed file, which has columns in gnuplot-friendly format.

```
#time hd-speed netspeed b_free b_busy b_loaded r_free r_busy testfor_def
0 0 42 1 0 12 0 0
1 0 0 41 2 0 11 1 0
2 0 2414 38 5 0 8 4 2414
3 771 6169 35 8 0 5 7 6169
```



## Hugepages ##
path: scripts/init\_hugepages
> scripts/deinit\_hugepages

The script has variables for how big the hugepages are and how much hugepage to reserver, which should be modified for your needs. Just remember to keep them aligned so an integer amount of pages fit into the reserved space eg. (BUFFERSIZE\*1024\*1024\*1024)/(HUGESIZE\*1024) = n, where n is an integer:
```
#!/bin/bash
#Size of pages in KB
HUGESIZE=2048
#Buffer size in GB
BUFFERSIZE=8
```

A systems hugepage size and capability can be queried with 'grep -i hugepage /proc/meminfo'.

Running init\_hugepages with the -c switch will query the number of gigabytes in maxmem and reserve nr\_hugepages accordingly.

When running the script, give the name of the user who will be running the vlbistreamer as parameter. The script requires root-privileges as it actually reserves space for the hugepages. If ./configure --enable-user=`<user>` (optional: --enable-group=`<group>`) was run, the installed init\_hugepages (not the one in vlbi-streamer/scripts) will remember the user and group settings and work automatically.

deinit\_hugepages removes the hugepages from use.

## Dealing with schedules ##

```
~/src/vlbi-streamer $ scripts/vbs_snpconvert 

scripts/vbs_snpconvert adds vlbistreamer commands to a snp-file. The output is a new snp-file with the name augmented to <snp>vbs.snp and a separate <snp>.recqueue script. The recqueue scripts can be run to queue the recording schedule to the vlbistreamer server.

If no host is given, the script will presume a vlbistreamer instance is running on localhost.

usage: scripts/vbs_snpconvert <snp-file> <options> <vbs_record options>

Options:
  -h <host> 	Prepare queue script for specific host
  -X 		Run queueing script after conversion.	

All extra options are added to each vbs_record command generated

```

Example:

```
kharn@viskiturska ~/src/vlbi-streamer $ scripts/vbs_snpconvert uwe_5b_mye001md.snp -w -q mark5bnet
Target file uwe_5b_mye001md.snp
Adding switch -w to extra ops
Adding switch -q to extra ops
Adding parameter mark5bnet to extra ops
Emptying old file uwe_5b_mye001md.snp.recqueue
kharn@viskiturska ~/src/vlbi-streamer $ head uwe_5b_mye001md.snp.recqueue #!/bin/bash
vbs_queue -m r mye001_Md_No0001 420 -e 1358332200 -w -q mark5bnet ;
vbs_queue -m r mye001_Md_No0002 420 -e 1358332800 -w -q mark5bnet ;
vbs_queue -m r mye001_Md_No0003 420 -e 1358333400 -w -q mark5bnet ;
vbs_queue -m r mye001_Md_No0004 420 -e 1358334000 -w -q mark5bnet ; printf "." ;
vbs_queue -m r mye001_Md_No0005 420 -e 1358334600 -w -q mark5bnet ;
vbs_queue -m r mye001_Md_No0006 420 -e 1358335200 -w -q mark5bnet ;
vbs_queue -m r mye001_Md_No0007 420 -e 1358335800 -w -q mark5bnet ;
vbs_queue -m r mye001_Md_No0008 420 -e 1358336400 -w -q mark5bnet ; printf "." ;
vbs_queue -m r mye001_Md_No0009 420 -e 1358337000 -w -q mark5bnet ;
kharn@viskiturska ~/src/vlbi-streamer $ ./uwe_5b_mye001md.snp.recqueue 
...........Done!
kharn@viskiturska ~/src/vlbi-streamer $ head -n 20 /usr/local/var/opt/vlbistreamer/schedule 

GbbCOQWNBRBT =
{
  filename = "mye001_Md_No0001";
  time = 420L;
  record = 1;
  #Schedule extra parameters
      starting_time = 1358332200L;
      wait_start_on_metadata = 1;
      datatype = "mark5bnet";
};

lloFWGLvcxAV =
{
  filename = "mye001_Md_No0002";
  time = 420L;
  record = 1;
  #Schedule extra parameters
      starting_time = 1358332800L;
      wait_start_on_metadata = 1;
      datatype = "mark5bnet";
};

```

## Mount Volumes ##

path: scripts/mount\_volumes

NOTE: Use with caution! Requires root privileges and formats drives! If used without caution, may result in data loss or full loss of operating system.

This script formats, tunes and mounts n hardrives and mounts them to /mnt/disk{i} where i is a sequential number starting from 0 and ending in n-1.

Running this the first time will prompt for which hard drives to mount. eg.

```
per@ara:~/src/vlbi-streamer$ scripts/mount_volumes 
 0: [ ]   sda
 1: [ ]   sdb
 2: [ ]   sdc
 3: [ ]   sdd
 4: [ ]   sde
 5: [ ]   sdf
 6: [ ]   sdg
 7: [ ]   sdh
 8: [ ]   sdi
 9: [ ]   sdj
10: [ ]   sdk
11: [ ]   sdl
12: [ ]   sdm
13: [ ]   sdn
14: [ ]   sdo
* = Use this drive
Input: c to continue with this setup, <number> to toggle use/don't use, set all on or off
Action? [[c]ontinue,<number>,[a]ll,e[x]it]: 
```

To skip this phase, you can augment the DRIVES-variable in the scripts early on and set it to include all the drives you want to use. This setup for example uses drives from sda to sdaj, and the latter specific drives.
```
  ...
DRIVES=($(echo sd{a..z} sda{a..j}))
DRIVES=(sdb sdc sdd sde sdf sdh sdj sdi sdm sdn sdk sdl sdo)
    ...
```

```
oper@ara:~/src/vlbi-streamer$ scripts/mount_volumes --help
usage: mount_volumes <options>
OPTIONS:
-f 		Automatically format all non-mounting volumes (Dangerous)
-a		Ask to format non-mounting volumes
-r		Rearrange all volumes(unmounts already mounted /mnt/disk volumes)
-j		Just mount the volumes, eventhought mounting with mountpoints failed. This is handy if you want to try a different filesystem and mount it fast
NOTE: will take a long time on the first run, but on later runs will 
simply remounts the volumes
```

The drives are formatted to EXT4 with mountoptions(stolen from Mark6 opts!)
```
  MOUNT_OPTS="defaults,data=writeback,noatime,nodiratime"
```

Also journaling is disabled. EXT4 could disable barriers, but since journaling is anyway disabled, it has no use.

## Test volumespeeds ##

path: scripts/test\_volumespeeds

The idea is to use tmpfs (which on default mounted to
/dev/shm/) and write/read/write+read from there to all the used drives
individually and parallel.

The script probes for mounts of devices sd|md and tries to write on each mountpoint. If the
probe fails, you can manually set the folders to the DRIVES variable array. dd uses
direct-flag and fdatasync so the vfs cache are ignored. Its better to run the test with
atleast 4GB file to get proper metrics

USAGE: ./test\_volumespeeds <size of test in MB>

Example output:
```
oper@ara:~/src/nettests$ ./test_volumespeeds 128
Generating testfile for 128 MB. This will take a while..
Generation done. Testing each disk individually
Testing Write to /mnt/disk0
Testing read from /mnt/disk0
Testing read+write on /mnt/disk0
Disk /mnt/disk0         - W: 128 MB/s R: 137 MB/s RW: 64 MB/s
Testing Write to /mnt/disk1
Testing read from /mnt/disk1
Testing read+write on /mnt/disk1
Disk /mnt/disk1         - W: 74 MB/s R: 69 MB/s RW: 27 MB/s
Testing Write to /mnt/disk2
Testing read from /mnt/disk2
Testing read+write on /mnt/disk2
Disk /mnt/disk2         - W: 115 MB/s R: 121 MB/s RW: 51 MB/s
Testing Write to /mnt/disk3
Testing read from /mnt/disk3
Testing read+write on /mnt/disk3
Disk /mnt/disk3         - W: 93 MB/s R: 65 MB/s RW: 40 MB/s
Testing Write to /mnt/disk4
Testing read from /mnt/disk4
Testing read+write on /mnt/disk4
Disk /mnt/disk4         - W: 119 MB/s R: 87 MB/s RW: 43 MB/s
Testing Write to /mnt/disk5
Testing read from /mnt/disk5
Testing read+write on /mnt/disk5
Disk /mnt/disk5         - W: 79 MB/s R: 86 MB/s RW: 35 MB/s
Testing Write to /mnt/disk6
Testing read from /mnt/disk6
Testing read+write on /mnt/disk6
Disk /mnt/disk6         - W: 79 MB/s R: 96 MB/s RW: 42 MB/s
Testing Write to /mnt/disk7
Testing read from /mnt/disk7
Testing read+write on /mnt/disk7
Disk /mnt/disk7         - W: 76 MB/s R: 108 MB/s RW: 42 MB/s
Testing Write to /mnt/disk8
Testing read from /mnt/disk8
Testing read+write on /mnt/disk8
Disk /mnt/disk8         - W: 79 MB/s R: 103 MB/s RW: 40 MB/s
Testing Write to /mnt/disk9
Testing read from /mnt/disk9
Testing read+write on /mnt/disk9
Disk /mnt/disk9         - W: 79 MB/s R: 109 MB/s RW: 41 MB/s
Testing Write to /mnt/disk10
Testing read from /mnt/disk10
Testing read+write on /mnt/disk10
Disk /mnt/disk10        - W: 79 MB/s R: 112 MB/s RW: 46 MB/s
Testing Write to /mnt/disk11
Testing read from /mnt/disk11
Testing read+write on /mnt/disk11
Disk /mnt/disk11        - W: 79 MB/s R: 111 MB/s RW: 40 MB/s
Testing Write to /mnt/disk12
Testing read from /mnt/disk12
Testing read+write on /mnt/disk12
Disk /mnt/disk12        - W: 82 MB/s R: 111 MB/s RW: 34 MB/s
Testing Write to /mnt/disk13
Testing read from /mnt/disk13
Testing read+write on /mnt/disk13
Disk /mnt/disk13        - W: 519 MB/s R: 581 MB/s RW: 238 MB/s
Testing parallel writes to all disks
Testing parallel reads on all disks
Testing parallel reads+writes to all disks
All Disks accumulated:  - W: 1680 MB/s R: 1680 MB/s RW: 1680 MB/s
All Disks parallel:     - W: 1039 MB/s R: 963 MB/s RW: 387 MB/s
Cleaning up
  
```