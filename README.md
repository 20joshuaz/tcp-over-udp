# CSEE 4119 PA2: Simplified TCP over UDP
Joshua Zhou, jz3311

### How to run
The Makefile generates executables for the client and server.
```
make
```

To run the client, do
```
./tcpclient <file> <address of udpl> <port of udpl> <window size> <ack port>
```

To run the server, do
```
./tcpserver <file> <listening port> <ack address> <ack port>
```

### Project Files
- `tcpclient.c` and `tcpserver.c` contain code for the client and server
- `validators.c/h` contains validators for input checking
- `tcp.c/h` defines a TCP segment and functions for operating on it
- `window.c/h` defines a window of TCP segments and functions for operating on it

### Features and Notes
- This project implements all the features listed in the directions, including
  - three-way handshake
  - reliable transmission of a file
  - connection teardown
  - transmission time adjustment
  - logging
- The code works as is. You can adjust some variables by changing the `define` macros at the top of `tcpclient.c` and `tcpserver.c`.
  - The timeout multiplier is set to 1.1. If it is set to 2 (as specified in the textbook), the file transfer sometimes stalls since the timeout increases too quickly.
- When resending segments after a timeout, the client uses a Go-Back-N policy. It sends all segments in the window.
  - GBN works better for this project since the server does not have a buffer for storing out-of-order segments. Reliable transfer still works with the TCP retransmission policy, but once one segment times out, all future segments also time out.
  - Sequence and ACK numbers are still based on the TCP policy.
- Because this project does not implement flow control, the receive window field is not used and is set to zero.

### Testing Environment
- I didn't account for endianness in this project. The program works on my M1 Mac.
