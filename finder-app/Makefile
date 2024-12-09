CROSS-COMPILE?=native

ifeq ($(CROSS-COMPILE), native)
	CC:=gcc
else ifeq ($(CROSS-COMPILE), aarch64-none-linux-gnu-)
	CC:=aarch64-none-linux-gnu-gcc
endif

all: writer

writer: writer.o
	${CC} ${CFLAGS} writer.o -o writer

writer.o: writer.c
	${CC} ${CFLAGS} -c writer.c -o writer.o

.PHONY: clean
clean:
	rm -f writer.o writer

# ifdef CROSS-COMPILE
# 	CC = $(CROSS_COMPILE)gcc
# else
# 	CC:=gcc
# endif

# ifeq ($(CROSS_COMPILE),)
# 	# Native build
# 	CC = gcc
# else
# 	# Cross-compilation
# 	CC = $(CROSS_COMPILE)gcc
# endif

# writer: writer.c
# 	$(CC) -o writer writer.c

# .PHONY: clean
# clean:
# 	rm -f writer.o writer