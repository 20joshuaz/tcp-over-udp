CC=gcc
CFLAGS=-g -Wall

libhelpers.a: helpers.o
	ar rcs libhelpers.a helpers.o

helpers.o: helpers.h

.PHONY: clean
clean:
	rm -f *.o *.a

.PHONY: all
all: clean libhelpers.a
