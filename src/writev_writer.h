#ifndef WRITEV_WRITER_H
#define WRITEV_WRITER_H
int writev_init_rec_entity(struct opt_s * opt, struct recording_entity * re);
int writev_write_stripped(int fd,long count,long offset,long packet_size);
#endif
