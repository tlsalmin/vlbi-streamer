# Introduction #

VLBI-streamer is designed to buffer observational data from a stations digital
backend. In practice this means saving UDP-packet streams from a network
connection at times specified by a schedule file.

## Setup ##

Detailed installation instructions are at [Building](Building.md). After succesfully building
and installing VLBI-streamer there are a few recommended steps.

### The data storage ###

Lets presume the machine has /dev/sda as the root drive and other drives are
freely usable for VLBI-streamer. To prepare these drives you can use the
mount\_volumes script with the -f switch, which will format and tune the drives
accordingly. So run mount\_volumes -f, Use a to select all, use 0 to deselect the
/dev/sda root drive and use c to continue and let the drives be formatted and
mounted to their correct locations at `/mnt/disk*` or whatever was specified as
ROOTDISK when invoking ./configure.

TODO: What would be the right way to automate mounts at boot? IMHO: Don't
add mount\_volumes to the vbs\_daemon script to run as service. Instead add a
switch to mount\_volumes, which appends the setup done by it to /etc/fstab. The
user would then only have to run mount\_volumes once and subsequent boots would
keep the setup persistent.

### Packet receiving ###

Since were receiving UDP packets at a high speed, some performance
considerations are in order. The kernel has packet buffers, which on default
quite small to save space and handle latency better. For our purposes we want to
use a large buffer to avoid packet loss. To accomplish this, one needs only to
add lines to /etc/sysctl.conf

```
net.core.wmem_max=16777216
net.core.rmem_max=16777216
```
These will set the buffer size to 16MB, which usually eliminates packet loss on
10GE networks with UDP-packets.

### Hugepages ###

Since were handling large amount of memory, using hugepages is an easy
performance enhancement. After choosing the amount of memory dedicated to
VLBI-streamer and setting it to the VLBI-streamer config file, running
init\_hugepages -c as root will take the correct amount of hugepages into use.
Note that its recommended to set the amount of hugepages into /etc/sysctl.conf
again to persist over reboots. Also the memory of a running system can become
too fragmented to facilitate the hugepages, so they are easier to initialize at
boot. The number of hugepages (2048 KB) can be calculated:
```
(<gigabytes> * 2 ^30)/(2048*2^10)
```
and set to /etc/sysctl.conf as:
```
  vm.nr_hugepages = <nr_pages>
```
When running init\_hugepages -c, the number of hugepages is shown, so this value
can also be copied instead of manually calculating it.

### Basic operations ###

The default run command for VLBI-streamer is /etc/init.d/vbs\_daemon start. This
is to imitate the regular service model on a debian system and also to grant
VLBI-streamer larger priority.

To let VLBI-streamer start automatically after boot invoke the command
```
  update-rc.d vbs_daemon defaults
```

Everything VLBI-streamer does is logged to a log file. Without specific prefix
appended, this should be in /usr/local/var/log/vlbistreamer.log. On the first
runs, I recommend opening a separate terminal and running
```
  tail -f /usr/local/var/log/vlbistreamer.log 
```
This way you can monitor what VLBI-streamer is doing and check for errors.


The basic manual commands for using VLBI-streamer are vbs\_send and vbs\_record.
Although the conversion of schedules from snp-files is automated, its good to do
a test run with dummy data before the actual sessions start. If you run
```
  vbs_record testrun 30
```
and then after a few seconds
```
  vbs_clearschedule
```
You should see activity in the test log.

### Setting the config file defaults for a specific digital backend ###
The VLBI-streamer config file can ease use quite dramatically. Anything set
there will be taken as a default option for all recordings. E.g. If you know you
will be receiving mark5b net frames from a fila10G to the port 46227, you can
set the values in the config file accordingly
```
  packet_size = 5016L;
  port = 46227;
  datatype = "mark5bnet";
```
Now all vbs\_record commands will automatically start from port 46227, use a
packet size of 5016 and presume the datatype is mark5bnet for rearranging the
frames. The same as invoking vbs\_record as
```
  vbs_record testrecording 30 -s 46227 -p 5016 -q mark5bnet
```

Now for actually recording some data you can use the fact that fila10G continuously
sends packets or use udpmon to send packets. After you have a test source ready,
start recording with
```
vbs_record testrecording 30 -s <socket> -p <packet size>
```

The logfile should show activity and the speed of recording. To monitor for
packet loss properly, you can use the ploss\_monitor script. If there is packet
loss at rates close to what will be used with normal recordings, the problem
should be debugged and dealt with. If there are packets lost, but ploss\_monitor
doesnt show them, they are very likely in the network between VLBI-streamer and
the backend and cannot be affected by tuning the VLBI-streamer machine.

### Buffers and recpoints ###
During a recording VLBI-streamer will show the amount of free, busy and loaded
recpoints and buffers.

The buffers are memory segments divided into filesizemb (in the config file)
blocks. A recording grabs a free memory buffer, fills it with received packets
and leaves it to write itself to disk after use. Continuous operations require
that the memory buffers have enough time to write themselves to disk to
guarantee free available memory buffers for the recording threads.

While testing with recordings dummy data it is useful to check that the
receiving process is stable e.g. There is a consistent amount of used recpoints
and memory buffers while the stream is being recorded. If the amount of used
buffers keeps increasing, the write backend or hard drives are not fast enough
to handle the stream speed. The recording will eventually fill all the memory
buffers and cause packet loss as there are no more free memory buffers.

To guarantee that the write backend is utilized efficiently, there should always
be more memory buffers than recpoints. If a limited amount of memory is
available, the filesizemb value should be dropped to increase division of the
allocated memory space. e.g. if you are using 8GB of memory, which is 16 512MB
blocks, you can double the number of memory buffers by setting filesizemb to 256
and use 30 hard drives more efficiently. The default memory value in the config
file is low to avoid OS troubles with using too much memory. The recommended
amount of used memory is ~ 1-2 GB below the maximum avaiable memory. So with
16GB of main memory, 14GB could be easily allocated to VLBI-streamer.
### Every day operations ###
After VLBI-streamer is working properly its use should happen mostly by using snp-files for timing. A guide on using snp-files is at [Scripts#Dealing\_with\_schedules](Scripts#Dealing_with_schedules.md). In addition to recording, the data storage should be managed to keep enough space available on the drives. This can be done with vbs\_ls and vbs\_rm for listing the recordings and removing them.