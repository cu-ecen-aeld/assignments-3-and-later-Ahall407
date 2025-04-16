CC ?= $(CROSS_COMPILE)gcc

CFLAGS ?= -g -Wall -Werror
LDFLAGS ?= -pthread -lrt

default : aesdsocket

all: aesdsocket

aesdsocket : aesdsocket.o
	$(CC) $(CFLAGS) $(LDFLAGS) aesdsocket.o -o aesdsocket

aesdsocket.o: aesdsocket.c
	$(CC) $(CFLAGS) $(LDFLAGS) -c aesdsocket.c

clean:
	-rm -f *.o aesdsocket