CC = gcc
CFLAGS = -Wall -g

all: oss user_proc

oss: oss.o utils.o
	$(CC) $(CFLAGS) -o oss oss.o utils.o

user_proc: user_proc.o utils.o
	$(CC) $(CFLAGS) -o user_proc user_proc.o utils.o

oss.o: oss.c resource.h utils.h
	$(CC) $(CFLAGS) -c -o oss.o oss.c

user_proc.o: user_proc.c resource.h utils.h
	$(CC) $(CFLAGS) -c -o user_proc.o user_proc.c

utils.o: utils.c utils.h
	$(CC) $(CFLAGS) -c -o utils.o utils.c

clean:
	rm -f *.o oss user_proc *.log
