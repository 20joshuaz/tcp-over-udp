CC=gcc
CFLAGS=-g -Wall -Ilibs/include
LDFLAGS=-Llibs/ars
LDLIBS=-ltcp -lhelpers

tcpserver:

tcpserver.o:

.PHONY: init
init:
	rm -rf libs
	/bin/sh ../getlibs.sh $(LDLIBS)

.PHONY: clean
clean:
	rm -rf *.o tcpserver libs

.PHONY: all
all:
	make clean
	make init
	cd libs && /bin/sh ./build.sh
	make

.PHONY: dockerbuild
dockerbuild: clean init
	docker build -t 20joshuaz/tcpserver .
