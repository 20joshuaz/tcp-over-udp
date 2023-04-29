#ifndef TCP_H
#define TCP_H

#include <stdint.h>

#define HEADER_LEN 20
#define MSS 576

#define ACK_FLAG 0x10
#define SYN_FLAG 0x02
#define FIN_FLAG 0x01

struct TCPHeader {
    uint16_t sourcePort;
    uint16_t destPort;
    uint32_t seqNum;
    uint32_t ackNum;
    uint8_t length;
    uint8_t flags;
    uint16_t recvWindow;
    uint16_t checksum;
    uint16_t urgentPtr;
};

struct TCPSegment {
    struct TCPHeader header;
    char data[MSS];

    int dataLen;
};

uint16_t calculateSumOfHeaderWords(struct TCPHeader header);
int isFlagSet(struct TCPHeader header, uint8_t flag);
struct TCPSegment makeTCPSegment(uint16_t sourcePort, uint16_t destPort, uint32_t seqNum, uint32_t ackNum,
                                 uint8_t flags, char *data, int dataLen);
int isChecksumValid(struct TCPHeader header);
void printTCPHeader(struct TCPHeader header);


#endif
