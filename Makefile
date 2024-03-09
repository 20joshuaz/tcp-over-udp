CC=gcc
CFLAGS=-g -Wall -Ilibtcp -Ilibhelpers
LDFLAGS=-Llibtcp -Llibhelpers
LDLIBS=-ltcp -lhelpers

.PHONY: all
all: tcpclient tcpserver

tcpclient:

tcpclient.o:

tcpserver:

tcpserver.o:

.PHONY: clean
clean:
	rm -rf *.o tcpserver tcpclient
