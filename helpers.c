#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int BUFFER_SIZE = 1000;
int HEADER_LEN = 20;
int MSS = 576;

void doBitwiseSum(uint16_t a, uint16_t b, uint16_t *resPtr, uint16_t *carryoverPtr) {
    uint16_t res = 0;
    int carryover = 0;
    for (int i = 0; i < 16; i++) {
        int currA = a >> i & 1;
        int currB = b >> i & 1;

        int sum = currA + currB + carryover;
        if (sum == 1) {
            res |= 1 << i;
            carryover = 0;
        }
        else if (sum == 2) {
            carryover = 1;
        }
        else if (sum == 3) {
            res |= 1 << i;
            carryover = 1;
        }
    }
    *resPtr = res;
    *carryoverPtr = carryover;
}

uint16_t calculateSumWithOverflow(uint16_t a, uint16_t b) {
    do {
        doBitwiseSum(a, b, &a, &b);
    } while (b);
    return a;
}

uint16_t getWord(char *segment, int offset) {
    return *(uint16_t *)(segment + offset);
}

void makeTCPHeader(char *segment, uint16_t sourcePort, uint16_t destPort, uint32_t seqNum, uint32_t ackNum,
                    int setACK, int setSYN, int setFIN) {
    // port numbers
    *(uint16_t *)segment = sourcePort;
    *(uint16_t *)(segment + 2) = destPort;

    // seq and ack numbers
    *(uint32_t *)(segment + 4) = seqNum;
    *(uint32_t *)(segment + 8) = ackNum;

    // header length
    *(uint8_t *)(segment + 12) = 5 << 4;  // 01010000

    // flags
    uint8_t *flags = (uint8_t *)(segment + 13);
    *flags = 0;
    uint8_t mask = 1;
    *flags |= mask * setFIN;
    *flags |= (mask << 1) * setSYN;
    *flags |= (mask << 4) * setACK;

    // receive window
    *(uint16_t *)(segment + 14) = 0;

    // urgent data pointer
    *(uint16_t *)(segment + 18) = 0;

    // checksum
    uint16_t checksum = 0;
    for (int i = 0; i <= 18; i += 2) {
        if (i != 16) {
            checksum = calculateSumWithOverflow(checksum, getWord(segment, i));
        }
    }
    checksum = ~checksum;
    *(uint16_t *)(segment + 16) = checksum;
}

int isNumber(char *s) {
    for (char *trav = s; *trav; trav++) {
        if (!isdigit(*trav)) {
            return 0;
        }
    }
    return *s;  // returns false for empty string
}

int getPort(char *portStr) {
    if (!isNumber(portStr)) {
        return 0;
    }
    int port = atoi(portStr);
    return (port >= 1024 && port <= 65535) * port;
}

int isValidIP(char *ip) {
    char *trav = ip;
    int numDots = 0;
    char curr;
    while ((curr = *trav++)) {
        numDots += (curr == '.');
    }
    if (numDots != 3) {
        return 0;
    }

    char ipCopy[strlen(ip) + 1];
    strcpy(ipCopy, ip);

    char delim[] = ".";
    char *token = strtok(ipCopy, delim);
    while (token) {
        if (!isNumber(token)) {
            return 0;
        }
        int part = atoi(token);
        if (!(part >= 0 && part <= 255)) {
            return 0;
        }
        token = strtok(NULL, delim);
    }
    return 1;
}
