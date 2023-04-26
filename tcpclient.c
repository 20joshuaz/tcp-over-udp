#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "helpers.h"

#define BUFFER_SIZE 1000
#define INITIAL_TIMEOUT 1
#define ALPHA 0.125
#define BETA 0.25

void updateRTTAndTimeout(int sampleRttPtr, int *estimatedRttPtr, int *devRttPtr, int *timeoutPtr, float alpha, float beta) {
    if(*estimatedRttPtr < 0) {
        *estimatedRttPtr = sampleRttPtr;
        *devRttPtr = sampleRttPtr / 2;
        *timeoutPtr = *estimatedRttPtr + 4 * *devRttPtr;
        return;
    }

    float newEstimatedRtt = (1 - alpha) * (float)*estimatedRttPtr + alpha * (float)sampleRttPtr;
    float newDevRtt = (1 - beta) * (float)(*devRttPtr) + beta * (float)abs(sampleRttPtr - *estimatedRttPtr);
    float newTimeout = newEstimatedRtt + 4 * newDevRtt;

    *estimatedRttPtr = (int)newEstimatedRtt;
    *devRttPtr = (int)newDevRtt;
    *timeoutPtr = (int)newTimeout;
}

void runClient(char *file, char *udplAddress, int udplPort, int windowSize, int ackPort) {
    int clientSocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(clientSocket < 0) {
        printf("error: failed to create socket\n");
        return;
    }

    struct sockaddr_in clientAddr;
    memset(&clientAddr, 0, sizeof(clientAddr));
    clientAddr.sin_family = AF_INET;
    clientAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    clientAddr.sin_port = htons(ackPort);
    if(bind(clientSocket, (struct sockaddr *)&clientAddr, sizeof(clientAddr)) < 0) {
        printf("error: failed to bind port\n");
        return;
    }

    struct sockaddr_in udplAddr;
    memset(&udplAddr, 0, sizeof(udplAddr));
    udplAddr.sin_family = AF_INET;
    udplAddr.sin_addr.s_addr = inet_addr(udplAddress);
    udplAddr.sin_port = htons(udplPort);

    DO_NOTHING_ON_ALARM

    struct TCPSegment segment;
    int seqNum = 0;
    uint32_t serverSeqNum;
    int timeout = INITIAL_TIMEOUT * 1e6;
    int isFirstTransmission;
    int sampleRtt;
    int estimatedRtt = -1;
    int devRtt;
    char serverMsg[BUFFER_SIZE];
    int serverMsgLen;

    segment = makeTCPSegment(ackPort, udplPort, seqNum++, 0, 0, 1, 0, NULL, 0);

    printf("log: sending connection request\n");
    isFirstTransmission = 1;
    do {
        if(sendto(clientSocket, &segment, HEADER_LEN, 0, (struct sockaddr *)&udplAddr, sizeof(udplAddr)) != HEADER_LEN) {
            printf("error: failed to send to socket\n");
            return;
        }

        errno = 0;
        ualarm(timeout, 0);
        serverMsgLen = (int)recvfrom(clientSocket, serverMsg, BUFFER_SIZE, 0, NULL, NULL);
        sampleRtt = (int)(timeout - ualarm(0, 0));
        if(errno == EINTR) {
            printf("warning: failed to receive SYNACK\n");
            isFirstTransmission = 0;
            timeout *= 2;
            continue;
        }
        if(serverMsgLen < 0) {
            printf("error: failed to read from socket\n");
            return;
        }
        segment = parseTCPSegment(serverMsg);
        if(isFirstTransmission) {
            updateRTTAndTimeout(sampleRtt, &estimatedRtt, &devRtt, &timeout, ALPHA, BETA);
        }
    } while(!(doesChecksumAgree(segment.header) && isSYNSet(segment.header) && isACKSet(segment.header)));

    serverSeqNum = segment.header.seqNum + 1;
    // segment = makeTCPSegment(ackPort, udplPort, );

    printf("log: received SYNACK, sending ACK (not implemented)\n");

    close(clientSocket);
}

int main(int argc, char **argv) {
    if(argc != 6) {
        printf("usage: tcpclient <file> <address of udpl> <port of udpl> <window size> <ack port>\n");
        return 0;
    }

    char *file = argv[1];
    char *udplAddress = argv[2];
    if(!isValidIP(udplAddress)) {
        printf("error: invalid udpl address\n");
        return 0;
    }
    int udplPort = getPort(argv[3]);
    if(!udplPort) {
        printf("error: invalid udpl port\n");
        return 0;
    }
    char *windowSizeStr = argv[4];
    if(!isNumber(windowSizeStr)) {
        printf("error: invalid window size\n");
        return 0;
    }
    int windowSize = (int)strtol(windowSizeStr, NULL, 10);
    int ackPort = getPort(argv[5]);
    if(!ackPort) {
        printf("error: invalid ack port\n");
        return 0;
    }

    runClient(file, udplAddress, udplPort, windowSize, ackPort);
}
