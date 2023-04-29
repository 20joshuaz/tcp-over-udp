#include <stdio.h>
#include <string.h>

#include "tcp.h"

int getNthBit(int num, int n) {
    return num >> n & 1;
}

int setNthBit(int num, int n) {
    return num | (1 << n);
}

void doBitwiseSum(uint16_t a, uint16_t b, uint16_t *resPtr, uint16_t *carryoverPtr) {
    uint16_t res = 0;
    int carryover = 0;
    for(int i = 0; i < 16; i++) {
        int currA = getNthBit(a, i);
        int currB = getNthBit(b, i);

        int sum = currA + currB + carryover;
        if(sum == 1) {
            res = setNthBit(res, i);
            carryover = 0;
        }
        else if(sum == 2) {
            carryover = 1;
        }
        else if(sum == 3) {
            res = setNthBit(res, i);
            carryover = 1;
        }
    }
    *resPtr = res;
    *carryoverPtr = carryover;
}

uint16_t calculateSumWithOverflow(uint16_t a, uint16_t b) {
    do {
        doBitwiseSum(a, b, &a, &b);
    } while(b);
    return a;
}

uint16_t calculateSumOfHeaderWords(struct TCPHeader header) {
    uint16_t *rawSegmentPtr = (uint16_t *)&header;
    uint16_t sum = 0;
    for(int i = 0; i < HEADER_LEN / 2; i++) {
        sum = calculateSumWithOverflow(sum, *(rawSegmentPtr + i));
    }
    return sum;
}

int isFlagSet(struct TCPHeader header, uint8_t flag) {
    return (header.flags & flag) != 0;
}

struct TCPSegment makeTCPSegment(uint16_t sourcePort, uint16_t destPort, uint32_t seqNum, uint32_t ackNum,
                                 uint8_t flags, char *data, int dataLen) {
    struct TCPHeader header;

    header.sourcePort = sourcePort;
    header.destPort = destPort;
    header.seqNum = seqNum;
    header.ackNum = ackNum;
    header.length = 0x50;  // 01010000
    header.flags = flags;
    header.recvWindow = 0;
    header.checksum = 0;
    header.urgentPtr = 0;

    uint16_t checksum = calculateSumOfHeaderWords(header);
    header.checksum = ~checksum;

    struct TCPSegment segment;
    segment.header = header;
    memcpy(segment.data, data, dataLen);

    segment.length = HEADER_LEN + dataLen;

    segment.expectedACKNum = seqNum;
    if(isFlagSet(header, SYN_FLAG) || isFlagSet(header, FIN_FLAG)) {
        segment.expectedACKNum++;
    }
    else if(!isFlagSet(header, ACK_FLAG)) {
        segment.expectedACKNum += dataLen;
    }

    return segment;
}

struct TCPSegment parseTCPSegment(const char *rawSegment) {
    return *(struct TCPSegment *)rawSegment;
}


int isChecksumValid(struct TCPHeader header) {
    uint16_t totalSum = calculateSumOfHeaderWords(header);
    for(int i = 0; i < 16; i++) {
        if(!getNthBit(totalSum, i)) {
            return 0;
        }
    }
    return 1;
}

void printTCPHeader(struct TCPHeader header) {
    fprintf(stderr, "{\n");
    fprintf(stderr, "\tsourcePort: %d\n", header.sourcePort);
    fprintf(stderr, "\tdestPort: %d\n", header.destPort);
    fprintf(stderr, "\tseqNum: %d\n", header.seqNum);
    fprintf(stderr, "\tackNum: %d\n", header.ackNum);
    fprintf(stderr, "\tlength: %d\n", header.length);
    fprintf(stderr, "\tflags: %x\n", header.flags);
    fprintf(stderr, "\trecvWindow: %d\n", header.recvWindow);
    fprintf(stderr, "\tchecksum: %x\n", header.checksum);
    fprintf(stderr, "\turgentPtr: %d\n", header.urgentPtr);
    fprintf(stderr, "}\n");
}
