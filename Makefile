CC = gcc
CFLAGS = -g -Wall
LDFLAGS = -g
# LDLIBS =

.PHONY: all
all: clean tcpclient tcpserver

tcpclient: validators.a tcp.a window.a

tcpclient.o:

tcpserver: validators.a tcp.a

tcpserver.o:

validators.a: validators.o
	ar rcs validators.a validators.o

validators.o:

tcp.a: tcp.o
	ar rcs tcp.a tcp.o

tcp.o:

window.a: window.o
	ar rcs window.a window.o

window.o:

.PHONY: clean
clean:
	rm -rf *.o *.a tcpserver tcpclient
