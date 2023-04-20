#include <ctype.h>
// #include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "helpers.h"

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

uint16_t calculateSumOfHeaderWords(struct TCPSegment segment) {
    uint16_t *rawSegmentPtr = (uint16_t *)&segment;
    uint16_t sum = 0;
    for(int i = 0; i < HEADER_LEN / 2; i++) {
        sum = calculateSumWithOverflow(sum, *(rawSegmentPtr + i));
    }
    return sum;
}

struct TCPSegment createTCPSegment(uint16_t sourcePort, uint16_t destPort, uint32_t seqNum, uint32_t ackNum,
        int setACK, int setSYN, int setFIN, char *data, int dataLen) {
    struct TCPSegment segment;

    segment.sourcePort = sourcePort;
    segment.destPort = destPort;
    segment.seqNum = seqNum;
    segment.ackNum = ackNum;

    segment.length = 5 << 4;  // 01010000

    uint8_t flags = 0;
    if(setFIN) {
        flags = setNthBit(flags, 0);
    }
    if(setSYN) {
        flags = setNthBit(flags, 1);
    }
    if(setACK) {
        flags = setNthBit(flags, 4);
    }
    segment.flags = flags;

    segment.recvWindow = 0;
    segment.checksum = 0;
    segment.urgentPtr = 0;

    uint16_t checksum = calculateSumOfHeaderWords(segment);
    segment.checksum = ~checksum;

    if(data) {
        strncpy(segment.data, data, dataLen);
    }

    return segment;
}

struct TCPSegment parseToTCPSegment(char *rawSegment, int segmentLen) {
    struct TCPSegment segment;
    strncpy((char *)&segment, rawSegment, segmentLen);
    return segment;
}

int isChecksumValid(struct TCPSegment segment) {
    uint16_t totalSum = calculateSumOfHeaderWords(segment);
    for(int i = 0; i < 16; i++) {
        if(!getNthBit(totalSum, i)) {
            return 0;
        }
    }
    return 1;
}

int isNumber(char *s) {
    for(char *trav = s; *trav; trav++) {
        if(!isdigit(*trav)) {
            return 0;
        }
    }
    return *s != 0;  // returns false for empty string
}

int getPort(char *portStr) {
    if(!isNumber(portStr)) {
        return 0;
    }
    int port = (int)strtol(portStr, NULL, 10);
    return (port >= 1024 && port <= 65535) * port;
}

int isValidIP(char *ip) {
    char *trav = ip;
    int numDots = 0;
    char curr;
    while((curr = *trav++)) {
        numDots += (curr == '.');
    }
    if(numDots != 3) {
        return 0;
    }

    char ipCopy[strlen(ip) + 1];
    strcpy(ipCopy, ip);

    char delim[] = ".";
    char *token = strtok(ipCopy, delim);
    while(token) {
        if(!isNumber(token)) {
            return 0;
        }
        int part = (int)strtol(token, NULL, 10);
        if(!(part >= 0 && part <= 255)) {
            return 0;
        }
        token = strtok(NULL, delim);
    }
    return 1;
}
