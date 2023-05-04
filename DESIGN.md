### Program Design
#### Client Walkthrough
The client initiates the three-way handshake. It keeps sending SYN segments until it receives a SYNACK from the server.
The client sends an ACK back. These actions are implemented using the built-in socket and timer functions.

The client then creates a window (written by me and implemented as a queue). The window holds all the segments currently in transit (segments that
have not been ACKed). The client opens the input file for reading and begins sending data until it fills the window. The client then
listens for an ACK. If the ACK number is greater than the lowest unACKed sequence number, then the client moves the window
forward and sends more segments.

If the client's timer goes off, then it sends all the segments in its window. I realize this is a Go-Back-N policy and not
a TCP one, but it seems to work better for this project (see more details in the Design Tradeoffs section).

When the client is finished sending the file, it sends a FIN. It keeps sending the FIN segment until it receives an
ACK. It then waits for a FIN from the server. When it receives one, it sends an ACK and starts a timer. The client
ACKs back to any additional FIN segments. When the timer goes off, the client terminates the program.

#### Server Walkthrough
When the program starts, the server waits and listens for a SYN segment. When it gets one, it responds with a SYNACK.
It keeps sending SYNACK segments until it receives an ACK from the client. These actions are implemented using the built-in socket and timer functions.

The server then open the output file for writing and listens for segments from the client. It keeps track of the next in-order sequence number.
When it receives a segment, it checks whether the segment has this sequence number. If so, it writes the data to the output file.
If not, it is discarded. The server then sends an ACK indicating the next sequence number that it expects. This ACK is sent regardless of whether
the received segment was used or discarded.

The server writes data until it receives a FIN. It then responds with an ACK and its own FIN. The server keeps sending this
FIN until it receives an ACK. The program then terminates.

### How It Works
#### TCP Segment
`tcp.h` contains `TCPSegment` struct that is used by the client and server. This struct contains TCP header fields and the
segment's data. Some notable header fields are
- seqNum: This indicates the sender's sequence number. It is incremented after SYN, FIN, and segments containing data are sent.
ACK segments do not increase the sequence number. This is specified in [RFC 761](https://www.ietf.org/rfc/rfc761.html).
- ackNum: This indicates what the sender expects the next sequence number from the receiver to be. ACKs are cumulative.
- flags: This can be set to indicate a SYN, FIN, and/or ACK segment.
- checksum: The checksum is computed using the method described in the textbook. Whenever the client or server receives a segment,
the first thing it does is check whether the checksum agrees with the rest of the header fields.

The fields are manipulated using bit operations.

#### Retransmission Timer Adjustment
Only the client performs retransmission timer adjustment since the server does not send enough non-ACK packets to warrant adjustments.
When the client sends segments, it chooses one of them and begins a timer. If it receives an ACK for the selected segment, it 
stops the timer. The elapsed time is the sample RTT, which is used to adjust the retransmission timer. The adjustments are made
according to [RFC 6298](https://www.rfc-editor.org/rfc/rfc6298).

On the other hand, if the client is required to resend the selected segment, it stops measuring its RTT. The client waits for
new segments to be sent to choose another segment to time.

If the retransmission timer goes off, the client increases it and restarts it. While the textbook specifies doubling the timer,
I increase it by just 10% (see more details in the Design Tradeoffs section). 

### Design Tradeoffs
- The timeout multiplier (what is multiplied to the retransmission timer after a timeout) is set to 1.1. 
  - If it is set to 2 (as specified in the textbook), the file transfer sometimes stalls since the timeout increases too quickly.
- When resending segments after a timeout, the client uses a Go-Back-N policy. It sends all segments in the window.
  - GBN works better for this project since the server does not have a buffer for storing out-of-order segments. 
  - I tried using the TCP retransmission policy, and while it still successfully performed reliable delivery, once one segment timed out, all future segments also timed out.
  - Sequence and ACK numbers are still based on the TCP policy.
- After receiving a FIN from the server, the client waits for 3 seconds before terminating.
  - This is a lot shorter than the textbook's 30 seconds, but I don't want to keep you waiting.
