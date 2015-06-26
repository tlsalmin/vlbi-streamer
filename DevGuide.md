# The architecture #

The architecture has been designed as modular as possible to fair with changes and new technologies. The main structs are:

|**struct** | **Responsibility** |
|:----------|:-------------------|
| streamer\_entity | Capturing of packets and invocation of writes |
| buffer\_entity | Management of buffer and managing writes |
| recording\_entity | Disk writes        |

The idea is that these entities are interchangeable, so if we for example want to receive RDMA data, he only needs to create a new streamer\_entity that handles the RDMA-data. Of course especially in RDMA there will be some overlap in the modules, which needs tuning

Each struct has a number of function pointers for their functionality and an option-struct which is specific to the back-end and type-casted in the start of each function if its required.  The rest of the methods are struct specific and set during the program start. Also there's helper functions for setting these parameters for a working set is recommended.

Other managerial structs for organizing the other entities are

| entity\_list\_branch | tree head with synchronization primitives |
|:---------------------|:------------------------------------------|
| listed\_entity       | Organization of similar struct            |
| schedule             | Schedule-specific top-level parts         |

# Details #

### Architecture ###

The different threads spawned:
  * A scheduler thread
  * A streamer thread for each queued receiver/sender management
  * the receiver/sender thread itself
  * A thread for each buffer.

The streamer and receiver/sender threads are separate due to legacy. They will be combined on the next refactoring pass.

The receiver thread asks the buffer branch for a free buffer. Upon receiving, it attempts to read data into that buffer from the socket until it is filled. After filling, the buffers own thread is woken up and the receiver gets another free buffer. The buffer thread queries for a free record point to write its data to, after which it will set itself free again.

The send side queries n buffer threads to load files in sequence. After a buffer has loaded a file from a mount point, it will set itself loaded with the files sequence number. The sender thread will block until a loaded buffer with the right sequence number appears and will then send it.

### Old architecture ###

The old architecture used a multiple receiver threads, which each had a buffer and a mount point.

Initial tests showed that having multiple threads for receiving and sending, thought scales better on multi-core processors, causes packet loss. During the development of the send threads rate limiter, which basically restricts the sender to send packets at a certain interval, we noticed that the wait time in between thread context switches was very long. So long that it was deduced that the 1-3% packet loss on multi-threaded receive was due to the network cards receive buffer overflowing, while the context switch was happening. On the send side this same effect caused the send speed to be limited in between 5 to 6 Gb/s.

## Modules ##

The three sections of the program are combined into three structs: streamer\_entity,
buffer\_entity and recording\_entity. These all have an entity-specific option-struct and a list
of function pointers, that make the software modular, as the central simple buffer can use these
pointers to operate any back-end.

## Back-ends for writing ##

Currently writing back-ends are libaio, default(just read &write) and splice. Writing your own
back-end is quite easy as you only need to implement the init, write and close functions(and
check if your using async io)(wait for async is also handy, but not yet implemented into
simple buffer, as it didn't work on aio). The common\_wrt.h has some common stuff to backends, which
you just need to set in your initializing function.

There are actually two initializing functions per back-end currently. One sets the functions and
calls init and the other is the init itself(open file, fallocate etc.). When done, implement a
switch/option bit for these into streamer.{c|h}.

## Back-ends for receiving ##

This is a bit trickier, as there are some mutex and signal stuff in receiving. The main thing
is to prevent the ringbuf from writing a buffer to disks, that hasn't been used yet. Other than
that its quite simple: Implement a init, receive/send loop, close and some small functions for
info, statistics and timed shutdown.

## Packet resequencing ##

The packet resequencing scheme is as follows:

1. We read the packet to the expected next packet slot (last received packet + 1)
> - If we'd only read the packets header, we'd still have to dump the packet out of the kernel receive buffer
> - If we'd read it to a temp-buffer we'd still have to do a memcpy to the correct spot
2. We give the header to the getseq-function that returns the correct sequence number according to the data type
3. If the sequence number is current + 1, we're happy and we return.
> - If we get an old packet, we copy the buffer to its correct slot.
> - If we get an newer packet, we jump our process to its location and copy the subsequent old packets to their correct places.

This algorithm enables us to work without remembering what we have received. This framework can also easily jump between files and the maximum used buffers at a time is two ( old if its missing packets and the current). Adding new data formats only requires implementing a new getseq-function and the rest of the framework can be kept intact. Of course the getseq function has to conform to the framework.

When implementing resending, the missed packets can be logged to an array with the indices. A good use for the streamer thread would be to organize the resending requests.

## Planned development ##

Tsunami UDP-interface: Tsunami seems to be widely used (Metsähovi-bias I know), so for easy
file access a tsunami backend would probably be useful. Remote mount of the files without the
fragmentation is probably easiest for the correlators?

Different sendends. How do the correlators want the data? Currently only sending with
udp-sockets.

Proper splice backend: vmsplice/splice hasn't lived up to its hype in the tests. Research on
this will take time, but the benefit might be worth it.

Packet resending on request