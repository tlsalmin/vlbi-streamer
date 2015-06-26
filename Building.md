

# Getting the source #

For newest git version clone the repo:
```
git clone https://code.google.com/p/vlbi-streamer/
```

# Building the streamer #

## Dependencies ##
Required:
  * libconfig
  * lpthreads
  * lrt

Libraries for realtime and pthreads are most likely already installed on an arbitrary linux system

Optional:
  * libaio

libaio can be disabled by adding the option '--enable-libaio=no' to the configure script as in: './configure --enable-libaio=no'. Autodetect in the ./configure-script should detect it missing as well.

### Installing depencies on debian/ubuntu ###

```
sudo apt-get install libaio-dev libconfig8-dev
```

## Building ##

### Building from a git clone ###
If you've cloned from the git-repo, you need to first run:
```
./autogen.sh
```

The rest is similar to the release installation

### Building from a release ###

```
./configure --enable-user=<user> --enable-group=<group>
make
sudo make install
```
Compiles the project and creates executable 'src/vlbistreamer' and installs it to /usr/local/bin.

Replace `<user>` with the user you want to run vlbistreamer with. This is for augmenting the schedule and log file. Do the same for group (can be the same as user or left out == same as user). This is the only configuring option that is mandatory!

Note: If you have compile errors about hugepages (MAP\_HUGETLB), disable them manually by configuring as
```
./configure --enable-hugepages=no
```

## Options ##
```
  --enable-hugepages         Enable support for hugepages.
  --enable-libaio         Include libaio backend.
  --enable-debug         Enable debug output.
  --enable-logtofile         Enable logging to file.
  --enable-daemon         Compile software as daemon(Default: yes).
  --enable-ppriority         Enable pthread priority setting (default: Yes).
  --enable-filesizemb         Set file size in MB (default: 512). <<< MOVED TO vlbistreamer.conf IN 0.5.1
  --enable-senddebug         Enable senddebug output.
  --enable-writegranuality         Enable write granuality (default: yes) Recommended no on large raids.
  --enable-user         Set the user.(default oper)
  --enable-group         Set the group.(default: flexbuf)
  --enable-multisenddebug         Enable multisend debug output.
  --enable-ratelimiter         Enable rate limiting in sending.(default: yes)

  ROOTDIRS    Prefix for disks organized as prefix<n>. Default = "/mnt/disk" >= disks are /mnt/disk0..n
  CFLAGS      C compiler flags

Always remember to set a =<yes,no or value> after the parameters.
```
Example: ./configure --enable-hugepages=no --enable-daemon=no

All ./configure options can be checked by running ./configure --help

If you want the data directories to be different than /mnt/disk, you can specify them as eg.

```
  ROOTDISK=/space ./configure --enable-user=user
```
This will set the rootdisk as /space0, /space1 and so on

Since vlbistreamer doesn't force any flags, its recommended to set atleast O2 for performance improvements. You're welcome to use anything higher, if you know how to use them.

```
  CFLAGS="-O2" ./configure {options}
```

### Hugepages ###

Hugepages are stored in kernel memory space. They help reduce page misses and were originally added to help with the splicewriters bad performance. There is a script in scripts/init\_hugepages that preps the hugepage-mount required. Details at [Scripts#Hugepages](Scripts#Hugepages.md).

Note that on machines that have been running a longer time, allocating the hugepages might fail since memory is too fragmented. For this it is recommended to set the final number of hugepages to /etc/sysctl.conf as
```
  vm.nr_hugepages = 512
```

This will reserve space for hugepages at bootup. Note that for the total amount of reserved memory you need to calculate your hugepages size. Total used memory for vlbistreaer = number\_of\_hugepages\*size\_of\_hugepages. You can check the default size of hugepages from /proc/meminfo. Its usually 2048KB. For example watt has 20GB of memory and dedicates 16GB of memory to vlbistreamer so it has
```
  vm.nr_hugepages = (16*2^30)/(2048*2^10) = 8192.
```

If the compile fails with MAP\_HUGETLB not defined, try using the configure-option '--enable-hugepages=no' to not compile hugepages into the program. A test for this will be added later, but is a bit more complex.

### Log to file ###

If --enable-logtofile=no, will direct all output to stdout and not log file.

### PPriority ###

If enabled, will give each thread a specific priority and will use chrt in vbs\_daemon to set a real-time priority foor vlbi-streamer. This is required for proper send speed controlling, since only with propep priorities can the threads sleep accurately. Also eliminated packet loss on Ara, the AMD machine used in testing, that occurred on receiving packets faster than 8Gb/s.

### User and group ###

Sets the user and group for vlbi-streamer. This is important, since its presumed that vlbistreamer is run as a user. Running it as root can starve the operating system too much of resources and can have bad side effects (especially if root is mounted over NFS..). Also many scripts will have a parameters for user that will help mounting the volumes etc.

### Daemon mode ###

The default running mode is daemon-mode. This is equivalent to common linux-daemons. A config file for common options will be installed in {prefix}/etc/vlbistreamer.conf, A schedule to {prefix}/var/opt/vlbistreamer/schedule and a log file to {prefix}/var/log/vlbistreamer.log. Without specifying a prefix the prefix is /usr/local. Daemon mode has better internal sharing of resources and supports sending of incomplete recordings.

Daemon mode is also the one better maintained as almost none of the development is done in non-daemon mode.

If daemon-mode is enabled, the preferred start method is:

sudo /etc/init.d/vbs\_daemon start

Since its an init-daemon, it can also be set to start on machine boot.

### Libaio ###

Libaio is an asynchronous IO-library that was originally designed as the only used write-end. Due to poor FS-support and functionality problems(wait-time not configurable) other backends we're introduced and the program structure split into two halves. Currently libaio still gives the best write performance, as it shows no IO\_WAIT time on the CPU:s when running below the maximum HD-write speed.

### Debug ###

Prints (A Lot of!) debug information on the software. Beware of overloading the console or ssh-connection. Its recommended to pipe the output locally to a file and then parse the output. Note: Error messages are not dependent on debug output.

### Multisenddebug ###

Prints (Even more!) debug information on the sending of packets by the threads. Will slow software considerably if not piped to file.

### Ratelimiter ###

Why do I still have this configurable?

## Tweaks ##
### Splicewrite ###
With atleast kernel 3.2.12 you can get pipe size increased from
64KB to 1MB for larger HD-writes. Ubuntu system thought doesn't seem to update the /usr/include/bits/fcntl.h nicely and doesn't give the new F\_GETPIPE\_SZ etc. flags for fcntl, so probably have to manually set these ENUM.

Increasing the pipe size will benefit in giving larger HD-writes, since the files are synced to disk after the write requests.

### FS settings ###
For quick formatting, tuning and mounting of hard-drives for the software you can use the script scripts/mount\_volumes described at [Scripts#Mount\_Volumes](Scripts#Mount_Volumes.md)

### NIC-related settings ###
Modern NICs including the Chelsio T3 on the used in developing vlbi-streamer have various settings adjusted by ethtool(2). The NIC can be run either in adaptive-rx, where the interrupt timing is adjusted by the kernel or manually, where the user can limit interrupts to have at least a certain amount of microseconds in between. Having interrupts fire uncontrolled can overload a single core on a 10Gbps interface. On the other hand a too long wait can cause the NICs receive buffer to overflow and cause packet loss. The optimal wait-time can be calculated from the expected rate of the network. If we for example have a 524287 byte receive buffer and we're receiving at 10Gb/s:
```
buffer=524287*8
rate=10*(2^30)
rate/buffer ~= 2560
```
Which is the number of times the 10Gb/s rate fills the buffer. This means the rx-limit should be at least 1000000/2560 ~= 390 microseconds plus some overhead to avoid overhead.

The receive and send side buffer sizes can be changed with sysctl as documented in (2). Its also good to enable large receive/send offloading if larger than MTU packets are in use.

### Eliminating packet loss on kernel end ###
To reduce the possibility of kernel receive buffer overflow, its recommended to increase the receive buffer size to ~16MB

Example:
```
  sudo sysctl net.core.rmem_max = 16777216
```
This setting can be made permanent between reboots by setting it to /etc/sysctl.conf

With lower rmem:s the software might experience packet loss, as getting a new buffer has some overhead, and the kernel packet buffer might overflow.