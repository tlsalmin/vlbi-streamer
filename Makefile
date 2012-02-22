CC = gcc 
CFLAGS = -g -O2
OBJECTS = 
EXEC = fanout
SRC = fanout.c

%.o : %.c
	$(CC) $(CFLAGS) -c $<

fanout : fanout.c
	$(CC) $(CFLAGS) $(SRC) -o $(EXEC)

clean:
	rm -f $(EXEC) $(OBJECTS)
