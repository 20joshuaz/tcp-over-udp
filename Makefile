CC = gcc
CFLAGS = -g -Wall
LDFLAGS = -g
# LDLIBS =

.PHONY: all
all: tcpserver tcpclient

tcpserver: validators.a tcp.a

tcpserver.o:

tcpclient: validators.a tcp.a linkedlist.a

tcpclient.o:

validators.a: validators.o
	ar rcs validators.a validators.o

validators.o:

tcp.a: tcp.o
	ar rcs tcp.a tcp.o

tcp.o:

linkedlist.a: linkedlist.o
	ar rcs linkedlist.a linkedlist.o

linkedlist.o:

.PHONY: clean
clean:
	rm -rf *.o *.a tcpserver tcpclient
