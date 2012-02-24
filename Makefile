CC = gcc 
CFLAGS = -g -O2 -Wall -march=amdfam10 -mabm -msse4a
OBJECTS = 
EXEC = fanout
SRC = fanout.c fanout.h

%.o : %.c
	$(CC) $(CFLAGS) -c $<

fanout : fanout.c fanout.h
	$(CC) $(CFLAGS) $(SRC) -o $(EXEC)

clean:
	rm -f $(EXEC) $(OBJECTS)
