#ifndef FANOUT
#define FANOUT
#ifndef PACKET_FANOUT
#define PACKET_FANOUT		18
#define PACKET_FANOUT_HASH		0
#define PACKET_FANOUT_LB		1
#endif
#define THREADED
#ifndef THREADED
#define THREADS 1
#else
#define THREADS 6
#endif

static int setup_socket(void);
static void fanout_thread(void);
static int close_fanout(void);
#endif //FANOUT
