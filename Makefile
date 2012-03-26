CC = gcc 
#CFLAGS = -g -O2 -Wall -march=amdfam10 -mabm -msse4a
#CFLAGS = -g -O2 -Wall
CFLAGS = -g -O0 -Wall
OBJECTS = fanout.o streamer.o udp_stream.o aioringbuf.o aiowriter.o common_wrt.o defwriter.o sendfile_streamer.o splicewriter.o
EXEC = streamer
LIBS= -lpthread -laio -lrt
SRC = fanout.h fanout.c common_wrt.h common_wrt.c udp_stream.c udp_stream.h streamer.c streamer.h aioringbuf.c aioringbuf.h aiowriter.c aiowriter.h defwriter.h defwriter.c sendfile_streamer.h sendfile_streamer.c splicewriter.h splicewriter.c

all : $(OBJECTS)
	$(CC) $(CFLAGS) $(LIBS) $(OBJECTS) -o $(EXEC)

%.o : %.c %.h
	$(CC) $(CFLAGS) $(LIBS) -c $<

clean:
	rm -f $(EXEC) $(OBJECTS)
