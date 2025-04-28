CC = gcc
CFLAGS = -Wall -g
DEPS = resource.h
OBJS = oss.o
USER_OBJS = user_proc.o

all: oss user_proc

oss: $(OBJS)
	$(CC) $(CFLAGS) -o oss $(OBJS)

user_proc: $(USER_OBJS)
	$(CC) $(CFLAGS) -o user_proc $(USER_OBJS)

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o oss user_proc *.log

.PHONY: all clean
