# ifdef CROSS-COMPILE
# 	CC = $(CROSS_COMPILE)gcc
# else
# 	CC:=gcc
# endif

ifeq ($(CROSS_COMPILE),)
	# Native build
	CC = gcc
else
	# Cross-compilation
	CC = $(CROSS_COMPILE)gcc
endif

writer: writer.c
	$(CC) -o writer writer.c

.PHONY: clean
clean:
	rm -f writer.o writer