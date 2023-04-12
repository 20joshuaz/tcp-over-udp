CC = gcc
CFLAGS = -g -Wall
LDFLAGS = -g
# LDLIBS =

tcpserver: tcpserver.o helpers.a

tcpserver.o:

helpers.a: helpers.o
	ar rcs helpers.a helpers.o

helpers.o:

.PHONY: clean
clean:
	rm -rf *.o *.a tcpserver
