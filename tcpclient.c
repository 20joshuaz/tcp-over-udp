#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "linkedlist.h"
#include "tcp.h"
#include "validators.h"

#define BUFFER_SIZE 1000
#define INITIAL_TIMEOUT 1
#define ALPHA 0.125
#define BETA 0.25

void doNothing(__attribute__((unused)) int signum) {}

void updateRTTAndTimeout(int sampleRTT, int *estimatedRTTPtr, int *devRTTPtr, int *timeoutPtr, float alpha, float beta) {
    assert(sampleRTT > 0);

    if(*estimatedRTTPtr < 0) {
        *estimatedRTTPtr = sampleRTT;
        *devRTTPtr = sampleRTT / 2;
        *timeoutPtr = *estimatedRTTPtr + 4 * *devRTTPtr;
        return;
    }

    float newEstimatedRTT = (1 - alpha) * (float)*estimatedRTTPtr + alpha * (float)sampleRTT;
    float newDevRTT = (1 - beta) * (float)*devRTTPtr + beta * (float)abs(sampleRTT - *estimatedRTTPtr);
    float newTimeout = newEstimatedRTT + 4 * newDevRTT;

    *estimatedRTTPtr = (int)newEstimatedRTT;
    *devRTTPtr = (int)newDevRTT;
    *timeoutPtr = (int)newTimeout;
}

int isSeqNumInRange(uint32_t seqNum, uint32_t baseSeqNum, uint32_t maxSeqNum) {
    if(maxSeqNum > baseSeqNum) {
        return baseSeqNum < seqNum && seqNum <= maxSeqNum;
    }
    return baseSeqNum < seqNum || seqNum <= maxSeqNum;
}

void runClient(FILE *file, char *udplAddress, int udplPort, int windowSize, int ackPort) {
    int clientSocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(clientSocket < 0) {
        perror("failed to create socket");
        fclose(file);
        exit(1);
    }

    struct sockaddr_in clientAddr;
    memset(&clientAddr, 0, sizeof(clientAddr));
    clientAddr.sin_family = AF_INET;
    clientAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    clientAddr.sin_port = htons(ackPort);
    if(bind(clientSocket, (struct sockaddr *)&clientAddr, sizeof(clientAddr)) < 0) {
        perror("failed to bind port");
        close(clientSocket);
        fclose(file);
        exit(1);
    }

    struct sockaddr_in udplAddr;
    memset(&udplAddr, 0, sizeof(udplAddr));
    udplAddr.sin_family = AF_INET;
    udplAddr.sin_addr.s_addr = inet_addr(udplAddress);
    udplAddr.sin_port = htons(udplPort);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &doNothing;
    sigaction(SIGALRM, &sa, NULL);

    struct TCPSegment segment;
    uint32_t seqNum = 0;
    uint32_t nextExpectedServerSeq;
    int timeout = (int)(INITIAL_TIMEOUT * 1e6);
    int isFirstTransmission;
    int timeRemaining;
    int estimatedRTT = -1;
    int devRTT;
    char serverMsg[BUFFER_SIZE];
    ssize_t serverMsgLen;

    segment = makeTCPSegment(ackPort, udplPort, seqNum++, 0, SYN_FLAG, NULL, 0);

    fprintf(stderr, "log: sending SYN\n");
    isFirstTransmission = 1;
    for(;;) {
        if(sendto(clientSocket, &segment, segment.length, 0, (struct sockaddr *)&udplAddr, sizeof(udplAddr)) != segment.length) {
            perror("failed to send to socket");
            close(clientSocket);
            fclose(file);
            exit(1);
        }

        errno = 0;
        ualarm(timeout, 0);
        serverMsgLen = recvfrom(clientSocket, serverMsg, BUFFER_SIZE, 0, NULL, NULL);
        timeRemaining = (int)ualarm(0, 0);
        if(errno == EINTR) {
            fprintf(stderr, "warning: failed to receive SYNACK\n");
            isFirstTransmission = 0;
            timeout *= 2;
            continue;
        }
        if(serverMsgLen < 0) {
            perror("failed to read from socket");
            close(clientSocket);
            fclose(file);
            exit(1);
        }
        segment = parseTCPSegment(serverMsg);
        if(isChecksumValid(segment.header) && segment.header.ackNum == segment.expectedACKNum && isFlagSet(segment.header, SYN_FLAG) && isFlagSet(segment.header, ACK_FLAG)) {
            if(isFirstTransmission) {
                updateRTTAndTimeout(timeout - timeRemaining, &estimatedRTT, &devRTT, &timeout, ALPHA, BETA);
            }
            break;
        }
    }

    nextExpectedServerSeq = segment.header.seqNum + 1;

    int windowCapacity = windowSize / MSS;
    struct LinkedList *window = newLinkedList(windowCapacity);
    if(!window) {
        perror("failed to malloc\n");
        close(clientSocket);
        fclose(file);
        exit(1);
    }
    uint32_t baseSeqNum;
    char fileBuffer[BUFFER_SIZE];
    size_t fileBufferLen;

    // TODO: seqNum += MSS
    // TODO: does server seqNum matter?
    fprintf(stderr, "log: received SYNACK, sending ACK\n");
    baseSeqNum = seqNum;
    segment = makeTCPSegment(ackPort, udplPort, seqNum++, nextExpectedServerSeq, ACK_FLAG, NULL, 0);
    offer(window, segment);
    if(sendto(clientSocket, &segment, segment.length, 0, (struct sockaddr *)&udplAddr, sizeof(udplAddr)) != segment.length) {
        perror("failed to send to socket");
        close(clientSocket);
        fclose(file);
        freeLinkedList(window);
        exit(1);
    }

    fprintf(stderr, "log: sending file\n");
    while(!isFull(window) && (fileBufferLen = fread(fileBuffer, 1, BUFFER_SIZE, file)) > 0) {
        segment = makeTCPSegment(ackPort, udplPort, seqNum, nextExpectedServerSeq, 0, fileBuffer, (int)fileBufferLen);
        offer(window, segment);
        seqNum += fileBufferLen;

        if(sendto(clientSocket, &segment, segment.length, 0, (struct sockaddr *)&udplAddr, sizeof(udplAddr)) != segment.length) {
            perror("failed to send to socket");
            close(clientSocket);
            fclose(file);
            freeLinkedList(window);
            exit(1);
        }
    }
    if(!fileBufferLen && ferror(file)) {
        perror("failed to read file");
        close(clientSocket);
        fclose(file);
        freeLinkedList(window);
        exit(1);
    }

    int remainingTimeout = timeout;
    isFirstTransmission = 1;
    while(!isEmpty(window)) {
        errno = 0;
        ualarm(remainingTimeout, 0);
        serverMsgLen = recvfrom(clientSocket, serverMsg, BUFFER_SIZE, 0, NULL, NULL);
        // sampleRtt = (int)(timeout - ualarm(0, 0));
        timeRemaining = (int)ualarm(0, 0);
        if(errno == EINTR) {
            fprintf(stderr, "warning: failed to receive ACK for seq %d\n", baseSeqNum);
            isFirstTransmission = 0;
            timeout *= 2;
            remainingTimeout = timeout;
            continue;
        }
        if(serverMsgLen < 0) {
            perror("failed to read from socket");
            close(clientSocket);
            fclose(file);
            freeLinkedList(window);
            exit(1);
        }
        segment = parseTCPSegment(serverMsg);
        if(isChecksumValid(segment.header) && isFlagSet(segment.header, ACK_FLAG) && isSeqNumInRange(segment.header.ackNum, baseSeqNum, seqNum)) {
            if(isFirstTransmission) {
                updateRTTAndTimeout(remainingTimeout - timeRemaining, &estimatedRTT, &devRTT, &timeout, ALPHA, BETA);
            }

            struct TCPSegment polledSegment;
            do {
                polledSegment = poll(window);
            } while(polledSegment.expectedACKNum != segment.header.ackNum);
            baseSeqNum = segment.header.ackNum;

            while(!isFull(window) && (fileBufferLen = fread(fileBuffer, 1, BUFFER_SIZE, file)) > 0) {
                segment = makeTCPSegment(ackPort, udplPort, seqNum, nextExpectedServerSeq, 0, fileBuffer, (int)fileBufferLen);
                offer(window, segment);
                seqNum += fileBufferLen;

                if(sendto(clientSocket, &segment, segment.length, 0, (struct sockaddr *)&udplAddr, sizeof(udplAddr)) != segment.length) {
                    perror("failed to send to socket");
                    close(clientSocket);
                    fclose(file);
                    freeLinkedList(window);
                    exit(1);
                }
            }
            if(!fileBufferLen && ferror(file)) {
                perror("failed to read file");
                close(clientSocket);
                fclose(file);
                freeLinkedList(window);
                exit(1);
            }

            remainingTimeout = timeout;
            isFirstTransmission = 1;
        }
        else {
            remainingTimeout = timeRemaining;
        }
    }

    // TODO
    segment = makeTCPSegment(ackPort, udplPort, seqNum++, nextExpectedServerSeq, FIN_FLAG, NULL, 0);
    assert(sendto(clientSocket, &segment, segment.length, 0, (struct sockaddr *)&udplAddr, sizeof(udplAddr)) == segment.length);

    close(clientSocket);
    fclose(file);

}

int main(int argc, char **argv) {
    if(argc != 6) {
        fprintf(stderr, "usage: tcpclient <file> <address of udpl> <port of udpl> <window size> <ack port>\n");
        exit(1);
    }

    char *udplAddress = argv[2];
    if(!isValidIP(udplAddress)) {
        fprintf(stderr, "error: invalid udpl address\n");
        exit(1);
    }
    int udplPort = getPort(argv[3]);
    if(!udplPort) {
        fprintf(stderr, "error: invalid udpl port\n");
        exit(1);
    }
    char *windowSizeStr = argv[4];
    if(!isNumber(windowSizeStr)) {
        fprintf(stderr, "error: invalid window size\n");
        exit(1);
    }
    int windowSize = (int)strtol(windowSizeStr, NULL, 10);
    if(windowSize / MSS < 2) {
        fprintf(stderr, "error: window size too small\n");
        exit(1);
    }
    int ackPort = getPort(argv[5]);
    if(!ackPort) {
        fprintf(stderr, "error: invalid ack port\n");
        exit(1);
    }

    char *fileStr = argv[1];
    FILE *file = fopen(fileStr, "rb");
    if(!file) {
        fprintf(stderr, "error: unable to read from file\n");
        exit(1);
    }

    runClient(file, udplAddress, udplPort, windowSize, ackPort);
}
