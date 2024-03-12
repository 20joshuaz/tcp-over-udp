CC=gcc
CFLAGS=-g -Wall -Ilibtcp -Ilibhelpers
LDFLAGS=-Llibtcp -Llibhelpers
LDLIBS=-ltcp -lhelpers

.PHONY: tcp
tcp: tcpclient tcpserver

tcpclient:

tcpclient.o:

tcpserver:

tcpserver.o:

.PHONY: clean
clean:
	rm -rf *.o tcpserver tcpclient

.PHONY: all
all: clean tcp
