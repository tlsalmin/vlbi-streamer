CC = gcc 
CFLAGS = -g -O2 -Wall -march=amdfam10 -mabm -msse4a
OBJECTS = 
EXEC = fanout
LIBS= -pthread
SRC = fanout.c fanout.h

%.o : %.c
	$(CC) $(CFLAGS) $(LIBS) -c $<

fanout : fanout.c fanout.h
	$(CC) $(CFLAGS) $(SRC) -o $(EXEC)

clean:
	rm -f $(EXEC) $(OBJECTS)
