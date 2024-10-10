# CROSS-COMPILE?=standard
# ifeq ($(CROSS-COMPILE),standard)
# 	CC:=gcc
# else
# 	CC:=aarch64-none-linux-gnu-gcc
# endif
ifdef CROSS-COMPILE
	CC:=aarch64-none-linux-gnu-gcc
else
	CC:=gcc
endif

writer: writer.o
	${CC} writer.o -o writer

writer.o: writer.c
	${CC} -c writer.c -o writer.o

.PHONY: clean
clean:
	rm -f writer.o writer