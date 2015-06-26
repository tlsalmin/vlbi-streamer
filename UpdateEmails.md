
### 2013 D8.08 test result mail ###
```
Hey ppl.

The Jive + 4 stations tests described in D8.08 were done succesfully on
28.3. Big thanks to all participants.

All the logs etc. are currently in the vlbi-streamer git master branch,
which is visible here:

http://code.google.com/p/vlbi-streamer/source/browse/#git%2Ftestresults%2Fd8_8_test_28_3_2013

Notes on the test setup:
------------------------
Recap: 4 stations sending @ 2Gb/s to JIVE, where data is correlated live.
At the same time the station flexbuffs are stressed with local recording.

when sending from both INAF and MH, there was a bottleneck in the SURFnet
side, which caused a 2x2Gb/s stream to squeese to 2x1Gb/s. This was
resolved later and we got 2x2Gb/s working again. Higher speeds would
result in packet loss though.

The JB GEANT link was down until thursday and when up it was restricted to
1870 Mb/s payload speed according to vlbistreamer.

Notes on the correlation:
-------------------------
The Jive flexbuff could handle 4x2Gb/s streams. Some extra stuff that it
had to perform:
 - Remove 8 bytes off each received packet so the correlator only sees
normal mark5b frames without the mark5b-net extra header.
 - Reorder the packet / fill pattern on lost packets.

Packets lost was between 0 - 200 per stream, which is ~0,0005%. Since the
traffic is UDP, some packet loss was expected.

Reading the individual files straight from /mnt/disk* worked but was very
slow as the correlator had to restart on each file.

vbs_fs was mounted and the correlator worked nicely on it after the
transfer was finished. When using it live (reading from vbs_fs a while the
file is beign recorded) worked until vbs_fs stalled. This wasn't
unexpected since its still under development and testing it wasn't really
planned. It was enough anyhow to show live correlation to work. More work
will be done to integrate vbs_fs and vlbistreamer and to speed up the
reading process.

Also part of the correlation process could be offloaded on to the
flexbuff machine without it causing packet loss on receive.

Notes on the local nodes:
-------------------------
INAF and JB nodes worked very well, especially when taking into account
that INAF was working with only 12 disks. It still managed to send at
2Gb/s and receive at 5.650 Gb/s without packet loss and with 2-3 disks and
1-2GB of memory to spare.

Watt worked well for a while, but due to me developing vbs_fs on it, it
had accumulated hundreds of ext4-dio-unwrit-processes which stalled the
whole system a few times during the receive. This of course caused large
packet loss, but didn't affect the sending badly. It also caused a
few outliers in the statistics. A reboot before the tests would have helped.

The onsala machine had a constant rate packet loss on the receive end.
Last time I saw this was with our AMD machine running without a larger
socket size or priority. I didn't want to take the risk of tuning the
machine for the test in yet-another environment so the receive graph is
pretty ugly on this site.

-Tomi
```
### 2013 March update email ###
```
Hey ppl.

Quicknote on the testdata for deliverable tests. Software done for adding mark5b net header and sending it as
udp-packets. Data is currently spread to Metsähovis Watt-machine.

Just some updates on vlbi-streamer. Two major points are Raid-performance and the fuse filesystem vbs_fs.

First after having some performance issues with Matteos raid 5 tanks I decided to do some tests on our local
machines comparing raid vs noraid suspecting the vlbistreamer infrastructure to suffer from a single writepoint.

I did a dummy receive test (receiving in a busyloop) to 14 drives without a raid and to different raid levels. All
the raids were running in software and we're spread out randomly over the controller cards so we could get the worst
case scenario. Some results on amount of bytes written in 30 seconds

w_noraid=50465865728
w_raid=44560285696
w_raid_nogran=49392123904
w_raid_nogran_aio=46707769344
w_raid5_nogran=26843545600
(w_noraid-w_raid)/w_noraid
11702127659574468085
(w_noraid-w_raid_nogran)/w_noraid
02127659574468085106
(w_noraid-w_raid_nogran_aio)/w_noraid
07446808510638297872
(w_noraid-w_raid5_nogran)/(w_noraid)
46808510638297872340

The nogran stands for cutting out granuality on writes. This means whole files (eg. 512MB) are written in a single
call, so the write spreads out better over the the array. (disable write granuality with ./configure
--enable-writegranuality=no)

This means that using raids under vlbistreamer shouldn't add overhead. The 2% overhead in w_raid_nogran can be
attributed to the software raid. More tests should be done though!

All the testruns etc. can be found in the vlbi-streamer repos testresults/raidtest.

https://code.google.com/p/vlbi-streamer/source/browse/testresults/raidtest?name=vbs_fs_dev
--------------

vbs_fs is a fuse filesystem for vlbistreamer to help with getting data to the correlator. It combines all the split
files to a single file and can be given offset-values to strip bytes from packets. Its available on code.google.com
vlbi-streamer git-repo in the vbs_fs_dev branch. Currently it supports the offset and read.

Examples:

vbs_fs -f 8 -r /mnt/mntmnt/disk temp

Combine all n drives /mnt/mntmnt/disk{0..n} to the temp-folder. On every file when read, strip from each packet 8
bytes from up front. This can be used to receive data as it comes from the fila10G (mark5b + 4byte filler + 4 byte
counter) and then show it to the correlator as normal mark5b data.

The problem ofc is reading speed, as the data is read from a single drive at a time. Although reads over file
boundaries are multithreaded, software hardle ever reads very large chunks at a time. Even a raid doesn't help,
since stripping the header at large speeds takes more cpu-time.

I've thought of integrating vbs_fs to vlbistreamer by making vlbistreamer use shm_open and sharing its buffers with
any other program. This would mean vlbistreamer accelerated reads in vbs_fs and doing the control signals through a
local socket. More on this after I've gotten some tests done, but the current branch can already set its buffers to
/dev/shm/simplebuffer_<buffer_number> and data can be read from there easily.

-Tomi

```

### 2013 January update email ###
```
Hey people. So after a delay vlbistreamer 0.5.0 is ready.                                                        
                                                                                                                 
http://vlbi-streamer.googlecode.com/files/vlbi-streamer-0.5.0.tar.gz                                             
                                                                                                                 
Main features:                                                                                                   
 - Filesize fixed to FILESIZEMB. Default is 512MB. Can be choosed with                                           
  ./configure --enable-filesizemb=<MB per file>                                                                  
  - Stream mirroring for RT-stuff (-n ip will mirror received stream)                                            
  - Added a bunch of tools for checking sequencing and metadata on                                               
packages.                                                                                                        
  This should be migrated to own contained library at a later point                                              
  - Active session tracking moved to central control with access control.                                        
  Allows arbitrary receiving and simultaneous sending of recordings.                                             
  - Added -f switch for stripping extra stuff from front of packets.                                             
-f n will                                                                                                        
  strip n bytes from the head of each packets. This is useful if the                                             
  correlator can't take mark5bnet frames which have an extra sequence                                            
number in                                                                                                        
  front. NOTE: This requires using writev as the writing end! TODO:                                              
Support  for libaio                                                                                              
  - snpconvert fixed and tested properly.                                                                        
    -Allow arbitrary extra options passed to queue-command. (eg.                                                 
        vbs_snpconvert <snp> -n <ip> will mirror each recording to <ip>                                          
        during  the sessiong)                                                                                    
  - vbs_rm accepts wildcards (Careful now!)                                                                      
  - a recording will exit if it receives too many wrong sized packets.                                           
(you can choose packet size per recording with -p <packet size> or                                               
specify it                                                                                                       
permanently in vlbistreamer.conf)                                                                                
  - Theres now a disk2file function which can also strip stuff from the                                          
  packets. usage: vbs_disk2file <recname> <outputfile> <options>. The                                            
  functionality will be replaced at some point with the fuse-filesystem.                                         
  - Lots of stuff in udp_streamer has been moved to generic functions in                                         
  common_filehandling so creating custom modules is easier.                                                      
  - Made a unionfs-script to mount all /mnt/disk* stuff to a single                                              
  /mnt/union for easier access.                                                                                  
  - Theres a last packet option -l <n> to receive exactly n packets                                              
  - Priority settings are now uniform inside vlbi-streamer and                                                   
  --enable-ppriority=yes is enabled on default. This will give                                                   
RT-priorities                                                                                                    
  to vlbistreamer and enables it accurate sleep, which again fixed                                               
busy-loop sending to reduce multiple sends overloading the cpu. Also                                             
fixed packet loss                                                                                                
  that appeared on our amd-machine with > 8Gb/s receive.                                                         
  - Added fillpattern for overwriting missing packets correctly. TODO:                                           
Whats  a "correct" fill pattern for each format?                                                                 
  - Added bunch of integration tests for faster checking to utests.                                              
make check  runs them all.                                                                                       
  - Added switch -w to wait for metadata with correct start time. Since                                          
  schedules give the recording start time automatically with -e, you                                             
can add the -w so vlbistreamer checks from the data stream metadata to                                           
start when  the metadata shows time = start time. It also sets the                                               
start time correctly,  so it will record the given time and not time -                                           
<time waited for correct  metadata>.                                                                             
  - Added switch -y to force socket reacquire. If there are overlapping                                          
  recordings on the same socket, this will try to reacquire as many times as                                     
  possible so the recording wont fail on a taken socket.                                                         
  - Datatype resequencing for mark5bnet supported. VDIF still not tested                                         
  properly due to new format with sequence number coming.                                                        
                                                                                                                 
I'll start updating the wiki next week and add I'll add a getting                                                
started guide for daemon mode. I'll also try to find every deprecated                                            
thing. I really recommend moving away from non-daemon mode.                                                      
                                                                                                                 
Next up in development: FUSE to show whole recording as a single file.                                           
        D8.8 tests etc.                                                                                          
                                                                                                                 
-Tomi                                                                  
```

### 2012 September update email ###
```
Hey people. Its been a while. Here's an update on whats been developed for
vlbi-streamer. Most of this is already on the wiki-pages at
http://code.google.com/p/vlbi-streamer/w/list

The newest release is available at the git repo or
http://vlbi-streamer.googlecode.com/files/vlbi-streamer-0.4.1.tar.gz

Testing and feedback are very welcome. A skim through will give an overview of the new
usage styles. The old style single invocation with a too long list of options is still
available though. I'll hopefully get to facilitate some demos at Haystack VLBI
workshop.

Server (aka. Daemon) mode:

A single invocation, service-like mode of running vlbi-streamer has been the main
focus of development. It enables proper timing of recordings, better sharing of
resources and simultaneous receiving and sending of recordings. Also this facilitates
service-level management of vlbi-streamer with automatic startup on boot etc.

Now when there are several recordings and readings on a single vlbi-streamer, all the
resources are shared among the events. A record point (eg. hard drive) can only be
accessed by a single memory buffer for a sequential event, so seek times are minimal.

Relevant usage changes:

Start up of software now with /etc/init.d/vbs_daemon <start|stop>.

All output directed to logfile (default: /usr/local/var/log/vlbistreamer.log). The
vbs_daemon script handles mounting of hugepages automatically if they are used.

Since its recommended to run vlbi-streamer in user-mode, theres a configure option for
it: ./configure --enable-user=<user>

This option is almost a requirement. After 'make;sudo make install;'
mount_volumes, init_hugepages and other scripts "remember" this setting and set the
permission to the mount points, hugepages, logfiles, schedule and config files
accordingly. Theres also a --enable-group=<group> option for group setups.

For a more direct response in error cases you can use --enable-logtofile=no to direct
all output to stdout and not the logfile.

Use vbs_record/vbs_send to queue new recordings/streams. These scripts update the
(default: /usr/local/var/opt/vlbistreamer/schedule) schedule file that vlbi-streamer
is monitoring. The scripts accept all the recording-specific options..

Usage:
vbs_record <experiment name> <time in seconds> <options>
vbs_send <experiment name> <target ip> <options>

Examples:
vbs_record experiment_n 300 #Record at default port for 300 seconds NOW

.. with an added -e for date:
vbs_record experiment_n 300 -s 2232 -e "5/26/2012 10:32:45"
#record for 300 seconds from port 2232 starting at the 26th of may

The -e switch accepts any time that the date-command accepts and converts it to POSIX
time for vlbi-streamer. Just remember to set it in quotes. If no time is given, the
recording/streaming is started immediately.
Some conversion Examples:
~ $ date -d "+5 days" +%s
1343725608
~ $ date -d "tomorrow +5 hours" +%s
1343398117
~ $ date -d "5/26/2011 10:32:45" +%s
1306395165
(more from man date)

~ $ for i in {0..4}; do vbs_record experiment_$i 300 -s $((2222+$i));done
Records 5 streams to ports 2222..2225 at the same time.

~ $ for i in {0..4} ; do vbs_send experiment_$i flexbuf.jive.nl -s $((2222+$i)); done
Send all the experiments to Jive simultaneously.

vbs_clearschedule can be used to empty the whole schedule. This will terminate all
running events. The schedule file can also be edited manually, with deleted events
terminating after removal. Scripts for listing the schedule with human readable dates
etc. and augmenting it are on the TODO list.

All configuration variables are now at a cfg-file (default
/usr/local/etc/vlbistreamer.conf).

Example options:
...
#Default socket port number
port = 2222;

#Maximum memory to use
maxmem = 4L;

#Number of disk drives ranging from /mnt/disk0 to /mnt/diskn
n_drives = 12;

#Set the expected receive datatype (unknown, vdif, mark5b, udpmon)
datatype = "udpmon";
...

These config files are accessed with libconfig inside vlbi-streamer. This has some
usage hinderance: All values are type safe, so long integers in the system have to
have an L following them. So when you're editing a long value, leave the L behind the
value. String values have quotes, all values end with ; and comments start with #.

The old single invocation with command line parameters also work if
vlbi-streamer is configured with --enable-daemon=no.

Running with snp-files:
A script names vbs_snpconvert was made to extract timing data from a snp-file.

usage: vbs_snpconvert <snp-file> <options>
Options:
  -h <host> Prepare queue script for specific host

vbs_snpconvert adds vlbistreamer commands to a snp-file. The output is a new snp-file
with the name augmented to <snp>vbs.snp and a separate
 <snp>.recqueue script. The recqueue scripts can be run to queue the recording
schedule to the vlbistreamer server.

After the conversion is ready, the schedule can be sent to the vlbi-streamer host by
running 'sh <snpfile>.recqueue'. TODO: Change separate ssh-commands to a single long
ssh command.

Packet resequencing:
A packet resequencing framework has been added and implementations made for udpmon and
vdif sequences. VDIF hasn't been tested enough, but once we get our DBBC with the
FILA10G running in test modes, I can run some proper tests on it. Helper programs like
check_seq_of_rec
<recording name> are added but currently they only support udpmon and so
are relevant only for benchmarking. The packets are resequenced live
before being written to disk.

There are also tools for scrambling udpmon-recordings in seqsrc. scramble_64
<filename> <byte spacing> <n> and scramble2_64 <filename> <filename2> <byte_spacing>
<n>  will scramble n packets in/between file/files for testing purposes.

Simultaneous send and receive of an event:
The -n <ip> switch will immediately resend every received packet to the ip given. This
will operate at the same rate as the receive rate.

I'll call a recording in the process of being recorded as a live event. If a live
event is scheduled to start sending, vlbi-streamer will combine the crucial parameters
inbetween the sending and receiving process. So if you're receiving from the antenna
at say 8Gbps and you have only a 2Gbps connection to Jive, you can stream the
recording at 2Gbps to Jive while its being recorded. The memory buffers are actively
scanned for lingering files, so that sending a file doesn't necessarily require it to
be loaded from disk.

Sending at a faster rate than is being recorded hasn't been tested properly yet. Also
its recommended to start the sending a few files after a receive has been started, but
solidifying the process is on the TODO list.

Statistics and analysis:
All logs with verbose = 1; can be parsed with the parse_logs script. It converts all
the human readable statistics to a gnuplottable file with data in columns. plot_parsed
can then be used to plot all the data.

Usage: scripts/plot_parsed <option> file-to-plot

OPTIONS: to plot:
  -d            drive speeds
  -n            network speeds
  -b            network buffers
  -r            recpoints
  -f <folder>   Alternate folder to save plots as images

Theres also statlogger, that logs the cpu-usage of vlbi-streamer and ploss_monitor,
that monitors the packet loss from kernel buffer overflows. vlbistreamer itself only
logs ploss occurring from packets in extremely bad sequence ( min +- 1 file, max +-2
files ). )

Management scripts:
The mount_volumes has been cleaned up and made more interactive. Currently it shows
all the mountpoints as a list eg.
 oper@ara:~/src/vlbi-streamer$ mount_volumes
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
15: [ ]   sdp
16: [ ]   sdq
17: [ ]   sdr
18: [ ]   sds
19: [ ]   sdt
20: [ ]   sdu
21: [ ]   sdv
* = Use this drive
Input: c to continue with this setup, <number> to toggle use/don't use, set all on or
off
Action? [[c]ontinue,<number>,[a]ll,e[x]it]:

usage: mount_volumes <options>
OPTIONS:
-f              Automatically format all non-mounting volumes (Dangerous)
-a              Ask to format non-mounting volumes
-r              Rearrange all volumes(unmounts already mounted /mnt/disk volumes)
-j              Just mount the volumes, eventhought mounting with mountpoints failed.
This is handy if you want to try a different filesystem and mount it fast

check_size <recording> //Gives the exact size of a recording + overhead from
.cfg-files

vbs_ls          //Lists all the available recordings.
                //Add -s to show also size

vbs_delete      //Delete a recording from all mountpoints

vbs_shutdown    //close vlbi-streamer cleanly. vbs_daemon uses this.

There are also scripts/testscripts and scripts/testscript2. These are highly local to
the MRO setup and are used for daily test runs for regression and bugs, so they most
likely cannot be used at different setups.

-Tomi

Hey ppl. Sorry for the spam, but I forgot to add a note on                                                     
vlbi-streamer installation procedure from the last mail.                                                       
                                                                                                               
Since its recommended to run vlbi-streamer in user-mode, theres a                                              
configure option for it: ./configure --enable-user=<user>                                                      
                                                                                                               
This option is almost a requirement. After 'make;sudo make install;'                                           
mount_volumes, init_hugepages and other scripts "remember" this                                                
setting and set the permission to the mount points, hugepages,                                                 
logfiles, schedule and config files accordingly. Theres also a                                                 
--enable-group=<group> option for group setups.                                                                
                                                                                                               
For a more direct response in error cases you can use                                                          
--enable-logtofile=no to direct all output to stdout and not the                                               
logfile.                                                                                                       
                                                                                                               
-Tomi                                                                

```

### 2012 May update email ###
```
Hey all.                                                                                                       
                                                                                                               
Lots of changes were done on vlbistreamer after the meeting and I've                                           
finally gotten the 0.2 version up on google code                                                               
                                                                                                               
http://vlbi-streamer.googlecode.com/files/vlbi-streamer-0.2.tar.gz                                             
                                                                                                               
The main changes were:                                                                                         
- Receiving/sending now done in a single thread                                                                
- Arbitrary packet size support                                                                                
- Ringbuffer was changes to a simpler buffer                                                                   
- Added some prioritytuning                                                                                    
- Added an RX-ring receive, that has some metadata(Sending not yet done)                                       
- disks and buffers are now requestable resources.                                                             
- Files are now split and contained in subfolders                                                              
                                                                                                               
I'll update the usage wiki today with info on the new parameters.                                              
After which I'll start changing the architecture to be more                                                    
daemon-like with cron-like behaviour and then build an bash-script                                             
architecture around it.                                                                                        
                                                                                                               
Testing the new version would be greatly appreciated! Expecially on                                            
performance anddropped packets, which are checked easily while                                                 
vlbistreamer is running with:                                                                                  
                                                                                                               
ps -A |grep -i vlbi |awk '{print $1}'| cat /proc/$1/net/udp                                                    
                                                                                                               
Note to testers: Don't use splice-writer yet.                                                                  
                                                                                                               
-Tomi                                                          
```

### 2012 April update email ###
```
Hey people. Just some vlbistreamer updates for testers. I'm very glad                                          
to hear Matteo Stagni and Jimmy Cullen are testing the software and I                                          
hope that affiliates with an aribox and some time could use the time                                           
to test the software.                                                                                          
                                                                                                               
1. Matteo Stagni from IRA-INAF in Bologna informed me that the git                                             
version of vlbistreamer requires a quite new autoconf package. I just                                          
happened to make the first release earlier that day, which "solves"                                            
this problem. So if you're having trouble with autoconf, just download                                         
the first release at:                                                                                          
http://vlbi-streamer.googlecode.com/files/vlbi-streamer-0.1.tar.gz and                                         
                                                                                                               
/configure                                                                                                     
make                                                                                                           
<sudo> make install                                                                                            
                                                                                                               
I'll have nightly builds uploaded manually to googlecode for more                                              
recent features, until I get it automated.                                                                     
                                                                                                               
2. I realised that you would probably appreciate some sort of verbose                                          
info on what speeds the software is running periodically (I've been                                            
staring at                                                                                                     
top with processors expanded mostly). I added a -v flag to the software,                                       
which will print the receiving sides speeds per second like so:                                                
                                                                                                               
oper@ara:~/src/vlbi-streamer$ src/vlbistreamer -n 13 -m r -w aio -p                                            
32768 -v -u utest 10                                                                                           
...                                                                                                            
STREAMER: Printing stats per second                                                                            
----------------------------------------                                                                       
Net Send/Receive completed:     8075Mb/s                                                                       
HD Read/write completed         4216Mb/s                                                                       
Dropped 0       Incomplete 0                                                                                   
Time 1s                                                                                                        
----------------------------------------                                                                       
Net Send/Receive completed:     7833Mb/s                                                                       
HD Read/write completed         6432Mb/s                                                                       
Dropped 0       Incomplete 0                                                                                   
Time 2s                                                                                                        
----------------------------------------                                                                       
Net Send/Receive completed:     7863Mb/s                                                                       
HD Read/write completed         7864Mb/s                                                                       
Dropped 0       Incomplete 0                                                                                   
Time 3s                                                                                                        
----------------------------------------                                                                       
Net Send/Receive completed:     8281Mb/s                                                                       
HD Read/write completed         7060Mb/s                                                                       
Dropped 0       Incomplete 0                                                                                   
Time 4s                                                                                                        
...                                                                                                            
                                                                                                               
TODO: Implement on send side. (The SW doesn't know how long it will                                            
send, so I need to change a bit of logic for this).                                                            
                                                                                                               
3. Libaio spews out "error: Success" when writing at near physical                                             
limit. This is probably due to querying too many write requests. The                                           
proper way to handle this would be io_wait, which doesn't work on our                                          
machines libaio. This Doesn't actually lose data since the sw doesn't                                          
mark the bytes as written, but does a bit of busy looping and is nasty                                         
ofc. Fix would probably be a check on number of io_requests and                                                
io_waiting when there are too many.          

                   4. The rate limiter is working. I added a switch -a MYYs (wAit) which                                          
will wait atleast MYY microseconds inbetween packets. Atleast at the                                           
start there will be larger gaps as data is read into memory from the                                           
disks.                                                                                                         
                                                                                                               
Note that the sending is showing a pretty poor performance (about                                              
6Gbps), which seems to imply that the sending method isn't that good.                                          
I was planning also a single thread send, where the other threads                                              
simple gather memory spots for a packet index, which would be required                                         
anyway for tsunamiudp. This will hopefully fix this issue.                                                     
                                                                                                               
5. There's some inconsistency with udpmon and vlbistreamer. When a                                             
received set is sent to udpmon_recv with vlbistreamer, it will log a                                           
lot of packets as lost. Testing with another vlbistreamer as a                                                 
receiver,                                                                                                      
it logged all the packets ok and recoreded sending the same bytecount.                                         
I don't know about the internals of udpmon, but I'll look into it.                                             
                                                                                                               
-Tomi                                                                                                       
```

### 2012 Original intro email ###

```
Hey everybody.                                                                                                 
                                                                                                               
vlbistreamer is now at a much more testable state. The send side is                                            
working, so it can be tested with simultaneous send and receive. Below                                         
is a description of the software in a hierarchy, with the main points                                          
first and more detailed stuff thereafter if youre interested. An up to                                         
date description is held at                                                                                    
http://code.google.com/p/vlbi-streamer/w/list                                                                  
I hope you have the time to test it and give feedback and critique, to                                         
which I'll accommodate the software to asap. Also the upcoming F2F                                             
meeting will be a good place for discussion.                                                                   
                                                                                                               
- Overall description -                                                                                        
                                                                                                               
vlbistreamers main task is to receive/send udp-data off/to the                                                 
network. All received data is saved to disks, with intermediate                                                
storage in per disk memory ringbuffers. The sending side fills these                                           
ringbuffers with data read earlier.                                                                            
                                                                                                               
The system runs on consumer hardware and doesn't use hardware-specific                                         
code. The idea is that the software scales as the hardware scales,                                             
without needing to code everything again in 5 years for new hardware.                                          
Thought the software is modular enough to implement your own hardware                                          
specific packet-receiver or writer backend if needed.                                                          
                                                                                                               
vlbistreamer is currently only an invocable program, with no listening                                         
server etc. These requirements are still open.                                                                 
                                                                                                               
- Getting started -                                                                                            
                                                                                                               
1. Get the git repository with. (I'll add an alpha release soon)                                               
                                                                                                               
git clone https://code.google.com/p/vlbi-streamer/                                                             
cd vlbi-streamer                                                                                               
                                                                                                               
2. Ready the disks.                                                                                            
                                                                                                               
A script to help the process was created at scripts/mount_volumes (its                                         
a bit crude)                                                                                                   
                                                                                                               
This script formats, tunes and mounts n hardrives and mounts them to                                           
/mnt/disk{i} where i is a sequential number starting from 0 and ending                                         
in n-1. (These could be read from a config file, but use cases are                                             
still a bit open.)                                                                                             
                                                                                                               
The hard drives you want to use should be specified into the script                                            
variable DRIVES. Be careful not to set the drive having the FS on this                                         
list. It shouldn't try to reformat it as its already mounted, but I                                            
cannot guarantee this.                                                                                         
                                                                                                               
Remembed to change the USER and GROUP variables to fit your system                                             
setting. These should be the user/group running vlbistreamer, which                                            
should have rw-access to these mountpoints                                                                     
                                                                                                               
When ready and safe, run                                                                                       
                                                                                                               
scripts/mount_volumes                                                                                          
                                                                                                               
NOTE: will take a long time on the first run, but on later runs will                                           
simply remount the volumes.                                                                                    
                                                                                                               
DEVSUGGESTION: Make USER and GROUP parameters. DRIVES could be a comma                                         
separated list, but even better we could use a add_volume-script that                                          
simply checks how many volumes are mounted and adds a given  one to                                            
the end or an empty spot. Software doesn't currently support holes in                                          
mountpoints.                                                                                                   
                                                                                                               
3. Compile vlbistreamer      
               The change from a hackish one Makefile to using autotools was quite                                            
hectic, so the compilation hasn't been tested on many systems. If you                                          
encounter errors, please let me know.                                                                          
                                                                                                               
4. <optional> Mount hugepages.                                                                                 
                                                                                                               
Theres a script for mounting hugepages if they're enabled on your                                              
kernel: scripts/init_hugepages. You can query your hugepage                                                    
size(default on script: 2048KB) from 'cat /proc/meminfo' and set it                                            
into the script. You can also change the amount of hugespacepage you                                           
want by changing the BUFFERSIZE-variable.                                                                      
                                                                                                               
Detailed guide at:                                                                                             
http://code.google.com/p/vlbi-streamer/wiki/Scripts#Hugepages                                                  
                                                                                                               
5. Ready to use.                                                                                               
                                                                                                               
A long detailed guide at:                                                                                      
http://code.google.com/p/vlbi-streamer/wiki/Usage                                                              
                                                                                                               
To get started quickly, just use a packet-generation capable software                                          
(like udpmon) to send the packets on another machine or send them to                                           
localhost:                                                                                                     
                                                                                                               
/udpmon_send -t <time> -u <port> -d <vlbistreamer ip> -p <packet size>                                         
                                                                                                               
and run vlbistreamer.                                                                                          
                                                                                                               
vlbistreamer -m r -p <packet size> -w <writer backend> -n <n. of                                               
harddrives> <name of session> <time>                                                                           
                                                                                                               
The m-switch here says to run in receiving-mode                                                                
                                                                                                               
An example run:                                                                                                
                                                                                                               
user@somewhere:~/udpmon_send -t 60 -u 2222 -d 192.168.0.1 -S 524287 -p                                         
8192 -w 8                                                                                                      
user@somewhereelse:~/vlbistreamer -s 2222 -m r -p 8192 -w aio -n 12 testrun 50                                 
                                                                                                               
After you've succesfully recorded your first session you can send the                                          
same session to a listener(like udpmon_recv)                                                                   
                                                                                                               
vlbistreamer -s <port> -m s -p <packet size> -w <reader backend> -n                                            
<n.of disks> <name of session> <destination ip>                                                                
                                                                                                               
An example run                                                                                                 
                                                                                                               
user@somewhere:~/udpmon_recv -u 2222                                                                           
user@somewhereelse:~/vlbistreamer -s 2222 -m s -p 8192 -w aio -n 12                                            
testrun 192.168.0.3                                                                                            
                                                                                                               
You can change the reader/writer backend to test the different                                                 
implementations. They work interchangably, so you can save the session                                         
with aio and the read it with default writer etc.                                                              
                                                                                                               
NOTE: Splicewriter doesn't have a functioning send-side yet.                                                   
                                                                                                               
- Technical aspects and devguide -                                                                             
                                                                                                               
Threads:                                                                                                       
                                                                                                               
The software uses two threads per used mountpoint: The                                                         
ringbuf+writer/reader thread and the receive/sender thread.                                                    
                                                                                                               
NOTE: Stuff below is about analogous to sending.
                          The receiver thread asks the ringbuf for a free spot in the buffer. Upon                                       
receiving, it attempts to read data into that buffer from the socket. As                                       
there are n (where n is the number of mountpoints) receivers, they all                                         
"compete" for the socket and share the load. The receiving thread also                                         
constructs a packet index according to the cumulated number of packets                                         
received so far. When it has received a certain amount of                                                      
packets(sized to accommodate large reads/writes) it will try to wake                                           
the ringbuf-thread to write the packets to disk.                                                               
                                                                                                               
The ringbuf simply waits for enough packets to accumulate into the                                             
buffer, so it can flush them to disk. On the send side it will attempt                                         
to keep the buffer as full as possible for the sender-thread to flush                                          
the packets to the network.                                                                                    
                                                                                                               
Modules:                                                                                                       
                                                                                                               
The three sections of the program are combined into three structs:                                             
streamer_entity, buffer_entity and recording_entity. These all have an                                         
entity-specific option-struct and a list of function pointers, that                                            
make the software modular, as the central ringbuf can use these                                                
pointers to operate any backend.                                                                               
                                                                                                               
Backends for writing:                                                                                          
                                                                                                               
Currenty writing backends are libaio, default(just read &write) and                                            
splice. Writing your own backend is quite easy as you only need to                                             
implement the init, write and close functions(and check if your using                                          
async io)(wait for async is also handy, but not yet implemented into                                           
ringbuf, as it didn't work on aio). The common_wrt.h has some common                                           
stuff to backends, which you just need to set in your initializing                                             
function.                                                                                                      
                                                                                                               
There are actually two initializing functions per backend currently.                                           
One sets the functions and calls init and the other is the init                                                
itself(open file, fallocate etc.). When done, implement a                                                      
switch/option bit for these into streamer.{c|h}.                                                               
                                                                                                               
Backends for receiving:                                                                                        
                                                                                                               
This is a bit trickier, as there are some mutex and signal stuff in                                            
receiving. The main thing is to prevent the ringbuf from writing a                                             
buffer to disks, that hasn't been used yet. Other than that its quite                                          
simple: Implement a init, receive/send loop, close and some small                                              
functions for info, statistics and timed shutdown.                                                             
                                                                                                               
- Planned development -                                                                                        
                                                                                                               
Rate limiter: The software currently sends at max speed. This could be                                         
easily changed with setting a wait time inbetween packets, which would                                         
also serve more reliable data transfers on lower bandwidth lines.                                              
                                                                                                               
Packet index recovery: Currently the software stalls, if we've lost a                                          
hard drive and cant find the next packet to send. Vlbistreamer should                                          
make a packet index at the start of sending and skip the missing                                               
packages.                                                                                                      
                                                                                                               
Tsunami UDP-interface: Tsunami seems to be widely used (Metsähovi-bias                                         
I know), so for easy file access a tsunami backend would probably be                                           
useful. Remote mount of the files without the fragmentation is                                                 
probably easiest for the correlators?                                                                          
                                                                                                               
Different sendends. How do the correlators want the data? Currently                                            
only sending with udp-sockets.               

      Nonflooding default writer: The tests between libaio seem to indicate                                          
that libaio does something right, as it doesn't push the hard drives  
         too much and so doesn't push the processors into IO_WAIT, which takes                                          
valuable resources away from sending/receiving. Near 8Gbps with 12                                             
disks libaio should only take 15% per core on a 6 core processor with                                          
no IO_WAIT time. A small increase in rate drops the available CPU time                                         
dramatically, so some sort of limiting should be used.                                                         
                                                                                                               
Proper splice backend: vmsplice/splice hasn't lived up to its hype in                                          
the tests. Research on this will take time, but the benefit might be                                           
worth it.                                                                                                      
                                                                                                               
CPU affinity away from NIC core: Receiving at 10Gbps takes a toll on                                           
one cores interrupts. One core should be dedicated to simply handling                                          
the interrupts for the packets.                                                                                
                                                                                                               
Autotools: The move to autotools was hectic so many dependencies and                                           
checks will arise and must be fixed.                                                                           
                                                                                                               
-Tomi                                                                                                                                                                                         
```