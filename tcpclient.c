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

struct Node {
    struct TCPSegment segment;
    struct Node *next;
};

struct Node *newNode(struct TCPSegment segment, struct Node *next) {
    struct Node *node = (struct Node *)malloc(sizeof(struct Node));
    if(!node) {
        perror("failed to malloc");
        exit(1);
    }
    node->segment = segment;
    node->next = next;
    return node;
}

void cleanup(int socket, FILE *file) {
    fclose(file);
    close(socket);
}

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

void runClient(FILE *file, char *udplAddress, int udplPort, int windowSize, int ackPort) {
    int clientSocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(clientSocket < 0) {
        perror("failed to create socket");
        cleanup(clientSocket, file);
        exit(1);
    }

    struct sockaddr_in clientAddr;
    memset(&clientAddr, 0, sizeof(clientAddr));
    clientAddr.sin_family = AF_INET;
    clientAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    clientAddr.sin_port = htons(ackPort);
    if(bind(clientSocket, (struct sockaddr *)&clientAddr, sizeof(clientAddr)) < 0) {
        perror("failed to bind port");
        cleanup(clientSocket, file);
        exit(1);
    }

    struct sockaddr_in udplAddr;
    memset(&udplAddr, 0, sizeof(udplAddr));
    udplAddr.sin_family = AF_INET;
    udplAddr.sin_addr.s_addr = inet_addr(udplAddress);
    udplAddr.sin_port = htons(udplPort);

    DO_NOTHING_ON_ALARM

    struct TCPSegment segment;
    uint32_t seqNum = 0;
    uint32_t serverACKNum;
    int timeout = INITIAL_TIMEOUT * 1e6;
    int isFirstTransmission;
    int sampleRtt;
    int estimatedRtt = -1;
    int devRtt;
    char serverMsg[BUFFER_SIZE];
    ssize_t serverMsgLen;

    segment = makeTCPSegment(ackPort, udplPort, seqNum++, 0, SYN_FLAG, NULL, 0);

    printf("log: sending connection request\n");
    isFirstTransmission = 1;
    for(;;) {
        if(sendto(clientSocket, &segment, segment.length, 0, (struct sockaddr *)&udplAddr, sizeof(udplAddr)) != HEADER_LEN) {
            perror("failed to send to socket");
            cleanup(clientSocket, file);
            exit(1);
        }

        errno = 0;
        ualarm(timeout, 0);
        serverMsgLen = recvfrom(clientSocket, serverMsg, BUFFER_SIZE, 0, NULL, NULL);
        sampleRtt = (int)(timeout - ualarm(0, 0));
        if(errno == EINTR) {
            printf("warning: failed to receive SYNACK\n");
            isFirstTransmission = 0;
            timeout *= 2;
            continue;
        }
        if(serverMsgLen < 0) {
            perror("failed to read from socket");
            cleanup(clientSocket, file);
            exit(1);
        }
        segment = parseTCPSegment(serverMsg);
        if(isFirstTransmission) {
            updateRTTAndTimeout(sampleRtt, &estimatedRtt, &devRtt, &timeout, ALPHA, BETA);
        }
        if(doesChecksumAgree(segment.header) && segment.header.ackNum == seqNum && isFlagSet(segment.header, SYN_FLAG) && isFlagSet(segment.header, ACK_FLAG)) {
            break;
        }
    }

    serverACKNum = segment.header.seqNum + 1;

    int windowCapacity = windowSize / MSS;
    int windowLength = 0;
    struct Node *head;
    char fileBuffer[BUFFER_SIZE];
    size_t fileBufferLen;

    // TODO: seqNum += MSS

    segment = makeTCPSegment(ackPort, udplPort, seqNum++, serverACKNum, ACK_FLAG, NULL, 0);
    head = newNode(segment, NULL);
    windowLength++;

    printf("log: received SYNACK, sending ACK and file\n");
    // isFirstTransmission = 1;
    while((fileBufferLen = fread(fileBuffer, 1, BUFFER_SIZE, file)) > 0) {

    }
    if(ferror(file)) {
        perror("failed to read file");
        cleanup(clientSocket, file);
        exit(1);
    }

    cleanup(clientSocket, file);
}

int main(int argc, char **argv) {
    if(argc != 6) {
        fprintf(stderr, "usage: tcpclient <file> <address of udpl> <port of udpl> <window size> <ack port>");
        exit(1);
    }

    char *fileStr = argv[1];
    FILE *file = fopen(fileStr, "r");
    if(!file) {
        fprintf(stderr, "error: unable to read file");
        exit(1);
    }
    char *udplAddress = argv[2];
    if(!isValidIP(udplAddress)) {
        fprintf(stderr, "error: invalid udpl address");
        exit(1);
    }
    int udplPort = getPort(argv[3]);
    if(!udplPort) {
        fprintf(stderr, "error: invalid udpl port");
        exit(1);
    }
    char *windowSizeStr = argv[4];
    if(!isNumber(windowSizeStr)) {
        fprintf(stderr, "error: invalid window size");
        exit(1);
    }
    int windowSize = (int)strtol(windowSizeStr, NULL, 10);
    if(windowSize / MSS < 2) {
        fprintf(stderr, "error: window size too small");
        exit(1);
    }
    int ackPort = getPort(argv[5]);
    if(!ackPort) {
        fprintf(stderr, "error: invalid ack port");
        exit(1);
    }

    runClient(file, udplAddress, udplPort, windowSize, ackPort);
}
