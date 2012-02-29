CC = gcc 
CFLAGS = -g -O2 -Wall -march=amdfam10 -mabm -msse4a
#CFLAGS = -g -O2 -Wall
OBJECTS = fanout.o streamer.o udp_stream.o
EXEC = streamer
LIBS= -lpthread
SRC = fanout.h fanout.c udp_stream.c udp_stream.h streamer.c streamer.h

all : $(OBJECTS)
	$(CC) $(CFLAGS) $(LIBS) $(OBJECTS) -o $(EXEC)

%.o : %.c %.h
	$(CC) $(CFLAGS) $(LIBS) -c $<

clean:
	rm -f $(EXEC) $(OBJECTS)
