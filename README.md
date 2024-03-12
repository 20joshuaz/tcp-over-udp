# CSEE 4119 PA2: Simplified TCP over UDP
Joshua Zhou, jz3311

This project was a programming assignment for my Networks class. See [Programming Assignment 2](https://github.com/20joshuaz/tcp-over-udp/blob/main/Programming%20Assignment%202.pdf) for the spec.

## How to run
First, create the library files by running `make` in `libtcp` and `libhelpers`.

The top-level Makefile generates executables for the client and server.
```
make
```

Package loss is simulated using `newudpl`. See more details [here](http://www.cs.columbia.edu/~hgs/research/projects/newudpl/newudpl-1.4/newudpl.html).
An executable is included in the repo. To run it, do

```
./newudpl [-p recv_port:send_port] [-i source_address] [-o destination_address] [-vv] [-L loss_rate]
```

If it doesn't work, you can build it from the provided tar file.

```
tar -xf newudpl-1.7.tar
cd newudpl-1.7.tar
./configure
make
```

To run the client, do
```
./tcpclient <file> <udpl address> <udpl port> <window size> <ack port>
```

To run the server, do
```
./tcpserver <file> <listening port> <ack address> <ack port>
```

An example of a valid run is

```
./newudpl -p 2222:3333 -i 127.0.0.1:1234 -o 127.0.0.1:4444 -vv -L50
./tcpserver README_copy.md 4444 127.0.0.1 1234
./tcpclient README.md 127.0.0.1 2222 10000 1234
```

## Project Files
- `tcpclient.c` and `tcpserver.c` contain code for the client and server
- `libhelpers/helpers.h` contains validators for input checking
- `libtcp/tcp.h` defines a TCP segment and functions for operating on it
- `libtcp/window.h` defines a window of TCP segments and functions for operating on it
- `DESIGN.md` describes the project's design
- `output.txt` shows a sample client-server interaction
  - Note that the client and server are capable of more types of logging than what is shown

## Bugs and Features
- This project implements all the features listed in the directions, including
  - three-way handshake
  - reliable transmission of a file
  - connection teardown
  - retransmission timer adjustment
  - logging
    - delivery, receipt, and timeouts during three-way handshake
    - delivery and receipt during file transfer (logging timeouts would result in too many messages)
    - delivery, receipt, and timeouts during connection teardown
    - fatal errors
- The code works as is. You can adjust some variables by changing the `define` macros at the top of `tcpclient.c` and `tcpserver.c`.
- The number of segments in the client's window is the inputted window size divided by (using integer division) the MSS.
- Because this project does not implement flow control, the receive window field is not used and is set to zero.
- The sequence numbers don't wrap around, so the largest file you can transfer is around 2<sup>32</sup> bytes.
- Though I haven't seen it happen, it is technically possible for the server to never quit because it never receives an ACK for its FIN. In this case, you can safely quit the program. The output file should be written to.

## Testing Environment
- Works on my M1 Mac and a VM running Ubuntu
