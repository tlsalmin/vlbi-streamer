
# Introduction #

Streamer is a multi threaded software for recording and streaming fast network streams. Vlbistreamer can be run in daemon mode, where it is constantly on and manages recordings / streams from a schedule file or in single invocation mode.

# Usage Daemon mode #

## vlbistreamer.conf ##
All global options for vlbistreamer are set in vlbistreamer.conf, which is located in {prefix}/etc/vlbistreamer.conf (Default: /usr/local/etc/vlbistreamer.conf). All options must end in semicolons. Switch options (like -u for use hugepages) are set as 0 or 1 for false or true. String-options must have hyphens around the value and some numerical values require an L in the end (identified as LONG-values) so if the original value has an L, leave the L there.

Heres the default config file included in the source package:
```
# payload of the received packets
packet_size = 7555L;

# Default socket port number. This is useful with for the fila10G, since the 
# destination port numbers wont most likely be changed. The config files usually 
# have 46227 as default target port.
port = 46227;

# Maximum memory to use.
maxmem = 4L;

# Number of disk drives ranging from /mnt/disk0 to /mnt/diskn
n_drives = 12;

# Which write-end to use. Options:  def, aio, writev, and splice (default: def)
# NOTE: for writes with stripping leading bytes (or offset) you need writev.
#writer = "def";

# Which capture type to use. Options fanout,udpstream,sendfile (default: udpstream) 
# Only udpstream is properly developed atm.
#capture = "udpstream";

# uncomment to use hugepages for memory buffers. Make sure init_hugepages 
# reserves the correct amount of memory as specified in maxmem. Else it will
# Crash at a random spot when it touches a memory pages that could not
# be reserved as a hugepages
#use_hugepage = 1;

# Specify to use verbose mode. Recommended as it shows receive speed etc.
verbose = 1;

# uncomment to use rx-ring for receiving. Not actively developed so most likely
# broken
#use_rx_ring = 1;

# Set the expected receive datatype (unknown, vdif, mark5b, mark5bnet,udpmon). 
# This option will set on package resequencing and the using of fill pattern 
# on missing packets. Note that if the data isn't really eg. mark5bnet but
# just random noise, the recording will fail, as all packets after the first
# will be discarded as out of order. Use this only if you're sure of the format
# that is received.
#datatype = "udpmon";

# Set the default file size. This shouldn't be changed lightly on a running system
# as already existing recordings cant be read back with vlbi-streamer(well not yet..)
# Its recommmended to set this only once when starting to use vlbistreamer. 
# The effect are described in the vlbistreamer wiki.
filesizemb = 512;
```

## Starting the daemon ##

```
  oper@ara:~/src/vlbi-streamer$ sudo /etc/init.d/vbs_daemon start
    
    OR

  oper@watt:~/src/vlbi-streamer$ sudo service vbs_daemon start
```

The motivation for running it with sudo is the ability to give it realtime priority with chrt.

The startup and status are visible in the logfile (default {prefix}/var/log/vlbistreamer.log which in default is /usr/local/var/log/vlbistreamer.conf). While running schedules, its recommended to open this file in following mode eg.

```
oper@watt:~/src/vlbi-streamer$ sudo service vbs_daemon start
2013-01-28 14:37:05+02:00 Checking if hugepages are in use..
2013-01-28 14:37:05+02:00 Initializing hugepages
Setting and creating 8192 of 2048 KB pages
2013-01-28 14:37:06+02:00 Vlbistreamer started
oper@watt:~/src/vlbi-streamer$ tail -f /usr/local/var/log/vlbistreamer.log 
Priority before sleep 0
Priority after sleep 20
Priority after setting priority 20
Running in daemon mode
Read config from /usr/local/etc/vlbistreamer.conf
Prepping recpoints and membuffers..Starting watch on statefile /usr/local/var/opt/vlbistreamer/schedule
Checking initial schedule
STREAMER: Printing stats per second
----------------------------------------
Running..
```

## Querying recordings/sends ##

```
// Just start recording now for 30 seconds and name the recording exp_2234
vbs_record exp2234 30

// Start recording after 60 seconds to port 35523 packets of size 5016
vbs_record exp2234 30 -e $(date -u -d "now + 60 seconds" ) -p 35523 -s 5016

// Start recording mark5bnet data starting at 1359368947 and wait for metadata to show this time before starting to record.
vbs_record exp2234 30 -e 1359368947 -w

// Send exp2234 to that.huge.storage.eu port 2222
vbs_send exp2234 that.huge.storage.eu -s 2222

// Send exp2234 to that.huge.storage.eu with a rate of a packet every 5 microseconds
vbs_send exp2234 that.huge.storage.eu -a 5
```

There are 3 scripts for adding new recordings: vbs\_queue, vbs\_send and vbs\_record. vbs\_queue is the main queuing script and the others are just wrappers for it. The commands take exactly the same parameters as vlbistreamer in [Usage#Usage\_non-daemon\_mode](Usage#Usage_non-daemon_mode.md). vbs\_record and vbs\_send simply add the -m {s|m} and give the same parameters to vbs\_queue. Also vbs\_record and vbs\_send write to the logfile

### Start times for streaming ###
Start times aren't generally something to worry about. The scripts vbs\_snpconvert [Scripts#Dealing\_with\_schedules](Scripts#Dealing_with_schedules.md) handles setting it automatically for snp-schedules.

The vbs-script commands also support the -e switch for a starting time. the e accepts a long integer in the format time since epoch, also known as Unix time. Since typing this manually is cumbersome, its recommended to use the unix date-command. date can take almost any kind of date syntax inside the -d "`<date>`" switch and giving it a format of +%s will output the unix time format. If no start time is given, the recording or streaming is started immediately. NOTE: Always use UTC time: The -u switch with date.

Examples:
```
~ $ date -u -d "+5 days" +%s
1343725608
~ $ date -u -d "tomorrow +5 hours" +%s
1343398117
~ $ date -u -d "5/26/2011 10:32:45" +%s
1306395165
```

### Other utilities ###

Check out the [Scripts](Scripts.md) page

## Examples ##
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
      -r RATE		Expected network rate in MB
      -s SOCKET	Socket number(Default: 2222)
      -e POSIX_TIME	start event at START_TIME (time since epoch or anything the date command accepts)
      -w		Wait for START_TIME in metadata before starting receive (End time will be moved accordingly)
      -v 		Verbose. Print stats on all transfers
      -y 		Force socket reaqcuire on error (Useful when recordings overlap)

...

//Basic timed receive
oper@ara:~/src/vlbi-streamer$ vbs_record testrecording 400 -e `date -u -d "now +20 seconds" +%s`
Recording added OK

...
//Basic send
oper@ara:~/src/vlbi-streamer$ scripts/vbs_send testrecording google.fi
Streaming added OK

...

Basic operations: Starting in first terminal..

oper@watt:~/src/vlbi-streamer$ sudo service vbs_daemon start
2013-01-28 14:41:47+02:00 Checking if hugepages are in use..
2013-01-28 14:41:48+02:00 Vlbistreamer started
oper@watt:~/src/vlbi-streamer$ tail -f /usr/local/var/log/vlbistreamer.log 
Priority before sleep 0
Priority after sleep 20
Priority after setting priority 20
Running in daemon mode
Read config from /usr/local/etc/vlbistreamer.conf
Prepping recpoints and membuffers..Starting watch on statefile /usr/local/var/opt/vlbistreamer/schedule
Checking initial schedule
STREAMER: Printing stats per second
----------------------------------------
Running..

Meanwhile in another terminal..
oper@watt:~/src/vlbi-streamer$ vbs_record testrecording2 10 -s 46227 -p 5016 -q mark5bnet 
Recording added OK

And the first terminal will start logging..

Adding new request with id string: UQqEsLhKGGzJ
New request is for session: testrecording2
Starting event testrecording2
STREAMER: In main, starting receiver thread 
UDP_STREAMER: Starting stream capture
2013-01-28 14:48:46+02:00: Added recording named testrecording2 to record for 10 s starting at UTC:  2013-01-28 12:48:46+00:00 
Time:	1359377326
Event:	testrecording2	Network:	392Mb/s	Dropped 0	Incomplete 0
HD-Speed:	0MB/s
Ringbuffers: Free:	31	Busy:	1	Loaded:	0
Recpoints: Free:	36	Busy:	0	Loaded:	0
----------------------------------------
Time:	1359377327
Event:	testrecording2	Network:	979Mb/s	Dropped 0	Incomplete 0
HD-Speed:	0MB/s
Ringbuffers: Free:	31	Busy:	1	Loaded:	0
Recpoints: Free:	36	Busy:	0	Loaded:	0
----------------------------------------
Time:	1359377328
Event:	testrecording2	Network:	979Mb/s	Dropped 0	Incomplete 0
HD-Speed:	0MB/s
Ringbuffers: Free:	31	Busy:	1	Loaded:	0
Recpoints: Free:	36	Busy:	0	Loaded:	0
...
Time:	1359377336
Event:	testrecording2	Network:	979Mb/s	Dropped 0	Incomplete 0
HD-Speed:	511MB/s
Ringbuffers: Free:	29	Busy:	3	Loaded:	0
Recpoints: Free:	34	Busy:	2	Loaded:	0
----------------------------------------
UDP_STREAMER: Closing streamer thread
STREAMER: Threads finished. Getting stats
Blocking until owned buffers are released
Time:	1359377337
Event:	testrecording2	Network:	584Mb/s	Dropped 0	Incomplete 0
HD-Speed:	511MB/s
Ringbuffers: Free:	29	Busy:	3	Loaded:	0
Recpoints: Free:	33	Busy:	3	Loaded:	0
----------------------------------------
Time:	1359377338
Event:	testrecording2	Network:	0Mb/s	Dropped 0	Incomplete 0
HD-Speed:	767MB/s
Ringbuffers: Free:	29	Busy:	3	Loaded:	0
Recpoints: Free:	33	Busy:	3	Loaded:	0
----------------------------------------

Socket was shutdown and recording will finish in writing to disk..
Time:	1359377349
Event:	testrecording2	Network:	0Mb/s	Dropped 0	Incomplete 0
HD-Speed:	255MB/s
Ringbuffers: Free:	31	Busy:	1	Loaded:	0
Recpoints: Free:	35	Busy:	1	Loaded:	0
----------------------------------------
Buffers finished
Recording testrecording2 finished OK
UDP_STREAMER: Closed
Stats for testrecording2 
Packets: 281533
Bytes: 1412169528
Dropped: 0
Incomplete: 0
Written: 1412169528
Recvtime: 10
Files: 3
HD-failures: 0
Net receive Speed: 1077Mb/s
HD write Speed: 1077Mb/s
Updated config file and removed testrecording2

...
//How to query 8 streams at the same time to different ports:

for i in {0..8}; do vbs_queue -s $((2222+$i)) streams_$i 300;done
```

Example vlbistreamer output:
```
Running in daemon mode
Read config from /usr/local/etc/vlbistreamer.conf
Prepping recpoints and membuffers..Starting watch on statefile /usr/local/var/opt/vlbistreamer/schedule
Checking initial schedule
Running..
Adding new request with id string: GeSTCAYQGHqR
New request is for session: streams_0
Starting event streams_0
STREAMER: In main, starting receiver thread 
Adding new request with id string: dfihZyEHDYkv
New request is for session: streams_1
Starting event streams_1
STREAMER: In main, starting receiver thread 
Adding new request with id string: dJPMBzTWHnbJ
New request is for session: streams_2
Starting event streams_2
STREAMER: In main, starting receiver thread 
Adding new request with id string: xuVjQtWNWQmJ
New request is for session: streams_3
Starting event streams_3
STREAMER: In main, starting receiver thread 
Adding new request with id string: YlDIBQMONbmv
New request is for session: streams_4
Starting event streams_4
STREAMER: In main, starting receiver thread 
Adding new request with id string: JASHjGRVrVOc
New request is for session: streams_5
Starting event streams_5
STREAMER: In main, starting receiver thread 
Adding new request with id string: AKdtpMELNDLp
New request is for session: streams_6
Starting event streams_6
STREAMER: In main, starting receiver thread 
Adding new request with id string: pNoOIhYHOIwF
New request is for session: streams_7
Starting event streams_7
STREAMER: In main, starting receiver thread 
Adding new request with id string: rMUxUYRdwwNZ
New request is for session: streams_8
Starting event streams_8
STREAMER: In main, starting receiver thread 
Socket shutdown: Transport endpoint is not connected
Socket shutdown: Transport endpoint is not connected
Socket shutdown: Transport endpoint is not connected
Socket shutdown: Transport endpoint is not connected
Socket shutdown: Transport endpoint is not connected
Socket shutdown: Transport endpoint is not connected
Socket shutdown: Transport endpoint is not connected
Socket shutdown: Transport endpoint is not connected
Socket shutdown: Transport endpoint is not connected
UDP_STREAMER: Closing streamer thread
UDP_STREAMER: Closing streamer thread
UDP_STREAMER: Closing streamer thread
UDP_STREAMER: Closing streamer thread
UDP_STREAMER: Closing streamer thread
UDP_STREAMER: Closing streamer thread
UDP_STREAMER: Closing streamer thread
UDP_STREAMER: Closing streamer thread
UDP_STREAMER: Closing streamer thread
Recording streams_0 finished OK
Stats for streams_0 
Packets: 2585903
Bytes: 22983505864
Dropped: 0
Incomplete: 0
Written: 206618927104
Time: 300
Files: 79
HD-failures: 0
Net receive Speed: 584Mb/s
HD write Speed: 5254Mb/s
Updated config file and removed streams_0
Recording streams_1 finished OK
Stats for streams_1 
Packets: 2582501
Bytes: 22953268888
Dropped: 0
Incomplete: 0
Written: 206618927104
Time: 300
Files: 79
HD-failures: 0
Net receive Speed: 583Mb/s
HD write Speed: 5254Mb/s
Updated config file and removed streams_1
Recording streams_2 finished OK
Stats for streams_2 
Packets: 2584273
Bytes: 22969018424
Dropped: 0
Incomplete: 0
Written: 206618927104
Time: 300
Files: 79
HD-failures: 0
Net receive Speed: 584Mb/s
HD write Speed: 5254Mb/s
Updated config file and removed streams_2
Recording streams_3 finished OK
Stats for streams_3 
Packets: 2630369
Bytes: 23378719672
Dropped: 0
Incomplete: 0
Written: 206618927104
Time: 300
Files: 81
HD-failures: 0
Net receive Speed: 594Mb/s
HD write Speed: 5254Mb/s
Updated config file and removed streams_3
Recording streams_4 finished OK
Stats for streams_4 
Packets: 2576778
Bytes: 22902402864
Dropped: 0
Incomplete: 0
Written: 206618927104
Time: 300
Files: 79
HD-failures: 0
Net receive Speed: 582Mb/s
HD write Speed: 5254Mb/s
Updated config file and removed streams_4
Recording streams_5 finished OK
Stats for streams_5 
Packets: 2583552
Bytes: 22962610176
Dropped: 0
Incomplete: 0
Written: 206618927104
Time: 300
Files: 79
HD-failures: 0
Net receive Speed: 583Mb/s
HD write Speed: 5254Mb/s
Updated config file and removed streams_5
Recording streams_6 finished OK
Stats for streams_6 
Packets: 2582471
Bytes: 22953002248
Dropped: 0
Incomplete: 0
Written: 206618927104
Time: 300
Files: 79
HD-failures: 0
Net receive Speed: 583Mb/s
HD write Speed: 5254Mb/s
Updated config file and removed streams_6
Recording streams_7 finished OK
Stats for streams_7 
Packets: 2545388
Bytes: 22623408544
Dropped: 0
Incomplete: 0
Written: 206618927104
Time: 300
Files: 78
HD-failures: 0
Net receive Speed: 575Mb/s
HD write Speed: 5254Mb/s
Updated config file and removed streams_7
Recording streams_8 finished OK
Stats for streams_8 
Packets: 2575717
Bytes: 22892972696
Dropped: 0
Incomplete: 0
Written: 206618927104
Time: 300
Files: 79
HD-failures: 0
Net receive Speed: 582Mb/s
HD write Speed: 5254Mb/s
Updated config file and removed streams_8
Shutdown scheduled
```

NOTE: HD write speeds are combined. This was a fault in an older version.

# Usage non-daemon mode #

```
oper@ara:~/src/vlbi-streamer$ src/vlbistreamer 
usage: src/vlbistreamer [OPTION]... name (time to receive / host to send to)
-d DRIVES	Number of drives(Default: 1)
-s SOCKET	Socket number(Default: 2222)
-u 		Use hugepages
-m {s|r}		Send or Receive the data(Default: receive)
-p SIZE		Set buffer element size to SIZE(Needs to be aligned with sent packet size)
-A MAXMEM	Use maximum MAXMEM amount of memory for ringbuffers(default 12GB)
-x 		Use an mmap rxring for receiving
-a MYY		Wait MYY microseconds between packet sends
-q DATATYPE	Receive DATATYPE type of data and resequence (DATATYPE: vdif, mark5b,udpmon)(Only UDPMON supporteed atm.)
-w {aio|def|splice|dummy}	Choose writer to use(Default: def)
-v 		Verbose. Print stats on all transfers
```
Example on receive old non-daemon receive:
```
oper@ara:~/src/vlbi-streamer$ src/vlbistreamer -d 12 -A 12 test4 200  -w splice -v -u -p 8888
STREAMER: Calculating total buffer size between 10 GB to 12GB, size 8888 packets, Doing maximum 16MB size writes
1887
STREAMER: Alignment found between 10 GB to 12GB, Each buffer having 798 MB, Writing in 17 MB size blocks, Elements in buffer 94208, Total used memory: 11179MB
STREAMER: In main, starting receiver thread 
STREAMER: Printing stats per second
----------------------------------------
Net Send/Receive completed: 	0Mb/s
HD Read/write completed 	0Mb/s
Dropped 0	Incomplete 0
Time 1s
Ringbuffers: Free: 13, Busy: 1, Loaded: 0
Recpoints: Free: 12, Busy: 0, Loaded: 0
----------------------------------------
Net Send/Receive completed: 	0Mb/s
HD Read/write completed 	0Mb/s
Dropped 0	Incomplete 0
Time 2s
Ringbuffers: Free: 13, Busy: 1, Loaded: 0
Recpoints: Free: 12, Busy: 0, Loaded: 0
----------------------------------------
Net Send/Receive completed: 	3089Mb/s
HD Read/write completed 	0Mb/s
Dropped 0	Incomplete 0
Time 3s
Ringbuffers: Free: 13, Busy: 1, Loaded: 0
Recpoints: Free: 12, Busy: 0, Loaded: 0
----------------------------------------
Net Send/Receive completed: 	7842Mb/s
HD Read/write completed 	2222Mb/s
Dropped 0	Incomplete 0
Time 4s
Ringbuffers: Free: 12, Busy: 2, Loaded: 0
Recpoints: Free: 11, Busy: 1, Loaded: 0

....
  
UDP_STREAMER: Closing streamer thread
Socket shutdown: Transport endpoint is not connected
Stats for test4 
Packets: 21958840
Bytes: 179886809088
Dropped: 0
Incomplete: 0
Written: 179886817280
Time: 200
HD-failures: 0
Net receive Speed: 6862Mb/s
HD write Speed: 6862Mb/s

```
Example on send:
```
oper@ara:~/src/vlbi-streamer$ src/vlbistreamer -d 12 -A 12 test6 192.168.0.3 -w aio -u -p 8888 -m s -a 20 -v   
Config found on /mnt/disk1/test6/test6.cfg
Config found on /mnt/disk2/test6/test6.cfg
Config found on /mnt/disk3/test6/test6.cfg
Config found on /mnt/disk4/test6/test6.cfg
Config found on /mnt/disk5/test6/test6.cfg
Config found on /mnt/disk6/test6/test6.cfg
Config found on /mnt/disk7/test6/test6.cfg
Config found on /mnt/disk8/test6/test6.cfg
Config found on /mnt/disk9/test6/test6.cfg
Config found on /mnt/disk10/test6/test6.cfg
Config found on /mnt/disk11/test6/test6.cfg
For recording test6: 23 files were found out of 23 total.
STREAMER: In main, starting receiver thread 
STREAMER: Printing stats per second
----------------------------------------
Net Send/Receive completed: 	0Mb/s
HD Read/write completed 	5138Mb/s
Dropped 0	Incomplete 0
Time 1s
Ringbuffers: Free: 0, Busy: 14, Loaded: 0
Recpoints: Free: 0, Busy: 12, Loaded: 0
----------------------------------------
Net Send/Receive completed: 	384Mb/s
HD Read/write completed 	9443Mb/s
Dropped 0	Incomplete 0
Time 2s
Ringbuffers: Free: 0, Busy: 14, Loaded: 0
Recpoints: Free: 0, Busy: 12, Loaded: 0
----------------------------------------
Net Send/Receive completed: 	3338Mb/s
HD Read/write completed 	11110Mb/s
Dropped 0	Incomplete 0
Time 3s
Ringbuffers: Free: 0, Busy: 14, Loaded: 0
Recpoints: Free: 0, Busy: 12, Loaded: 0
  
...
Net Send/Receive completed: 	3339Mb/s
HD Read/write completed 	0Mb/s
Dropped 0	Incomplete 0
Time 50s
Ringbuffers: Free: 12, Busy: 1, Loaded: 1
Recpoints: Free: 12, Busy: 0, Loaded: 0
----------------------------------------
Net Send/Receive completed: 	3190Mb/s
HD Read/write completed 	0Mb/s
Dropped 0	Incomplete 0
Time 51s
Ringbuffers: Free: 14, Busy: 0, Loaded: 0
Recpoints: Free: 12, Busy: 0, Loaded: 0
----------------------------------------
Stats for test6 
Packets: 2092065
Bytes: 18421064376
Read: 18594275328
Time: 0s
HD-failures: 0

```

Streamer will create files with <recording name>.cfg and <recording name>.<index of received file> onto each mountpoints subfolder, where the cfg file contains packet size, number of files, filesize, total number of packets and the buffer division.

---

NOTE/TODO: The mountpoints are hard coded ATM. As the final use-cases determine the best way to give these mountpoints to the software. These files are setup as /mnt/diskn, where n is a number 0..n\_of\_drives. You can also set the ROOTDIRS variable to point somewhere else to set the mountpoints.

TODO: Splice not tested thoroughly yet.
