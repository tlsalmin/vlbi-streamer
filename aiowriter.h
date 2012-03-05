#ifndef AIOWRITER
#define AIOWRITER
//Stuff stolen from
//http://stackoverflow.com/questions/8629690/linux-async-io-with-libaio-performance-issue
int aiow_init(void * ringbuf, void * recpoint);
int aiow_write(void * ringbuf, void * recpoint);
int aiow_check(void * recpoint);
int aiow_close(void * ioinfo, void * ringbuf);
int aiow_wait_for_write(void * recpoint, double timeout);
#endif
