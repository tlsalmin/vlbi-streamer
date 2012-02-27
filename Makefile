CC = gcc 
#CFLAGS = -g -O2 -Wall -march=amdfam10 -mabm -msse4a
CFLAGS = -g -O2 -Wall
OBJECTS = fanout.o streamer.o
EXEC = streamer
LIBS= -pthread
SRC = fanout.h fanout.c streamer.c streamer.h

all : $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $(EXEC)

%.o : %.c %.h
	$(CC) $(CFLAGS) $(LIBS) -c $<

clean:
	rm -f $(EXEC) $(OBJECTS)
