CC=gcc
CFLAGS=-g -Wall

libtcp.a: tcp.o window.o
	ar rcs libtcp.a tcp.o window.o

tcp.o: tcp.h

window.o: window.h tcp.h

.PHONY: clean
clean:
	rm -f *.o *.a

.PHONY: all
all: clean libtcp.a
