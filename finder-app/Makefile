ifdef CROSS-COMPILE
	CC:=aarch64-none-linux-gnu-gcc
else
	CC:=gcc
endif

writer:
	${CC} -o writer writer.c

.PHONY: clean
clean:
	rm -f writer.o writer