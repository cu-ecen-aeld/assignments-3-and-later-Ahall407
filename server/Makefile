CC ?= $(CROSS_COMPILE)gcc

CFLAGS ?= -g -Wall -Werror
CFLAGS += -I../aesd-char-driver
LDFLAGS ?= -pthread -lrt

# Allow setting USE_AESD_CHAR_DEVICE from the command line, default to 1
USE_AESD_CHAR_DEVICE ?= 1

# If USE_AESD_CHAR_DEVICE=1, add -DUSE_AESD_CHAR_DEVICE to CFLAGS
ifeq ($(USE_AESD_CHAR_DEVICE),1)
CFLAGS += -DUSE_AESD_CHAR_DEVICE
endif

default : aesdsocket

all: aesdsocket

aesdsocket : aesdsocket.o
	$(CC) $(CFLAGS) aesdsocket.o $(LDFLAGS) -o aesdsocket

aesdsocket.o: aesdsocket.c
	$(CC) $(CFLAGS) -c aesdsocket.c

clean:
	-rm -f *.o aesdsocket