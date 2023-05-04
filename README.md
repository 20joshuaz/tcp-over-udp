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
- Makefile
- `README.md`
- `DESIGN.md` describes the project's design
- `output.txt` shows a sample client-server interaction
  - Note that the client and server are capable of more types of logging than what is shown

### Bugs and Features
- This project implements all the features listed in the directions, including
  - three-way handshake
  - reliable transmission of a file
  - connection teardown
  - retransmission timer adjustment
  - logging
    - delivery, receipt, and timeouts during three-way handshake
    - delivery and receipt during file transfer (logging timeouts would result in too many messages)
    - delivery, receipt, and timeouts during connection teardown
- The code works as is. You can adjust some variables by changing the `define` macros at the top of `tcpclient.c` and `tcpserver.c`.
- The number of segments in the client's window is the inputted window size divided by (using integer division) the MSS.
- Because this project does not implement flow control, the receive window field is not used and is set to zero.
- The sequence numbers don't wrap around, so the largest file you can transfer is around 2<sup>32</sup> bytes.
- Though I haven't seen it happen, it is technically possible for the server to never quit because it never receives an ACK for its FIN. In this case, you can safely quit the program. The output file should be written to.

### Testing Environment
- I didn't account for endianness in this project. It works on my M1 Mac and a VM running Ubuntu.
