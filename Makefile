CC = gcc
CFLAGS = -g -Wall
LDFLAGS = -g
# LDLIBS =

.PHONY: all
all: tcpserver tcpclient

tcpserver: helpers.a

tcpserver.o:

tcpclient: helpers.a

tcpclient.o:

helpers.a: helpers.o
	ar rcs helpers.a helpers.o

helpers.o:

.PHONY: clean
clean:
	rm -rf *.o *.a tcpserver tcpclient
