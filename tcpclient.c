#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "window.h"
#include "tcp.h"
#include "validators.h"

#define SI_MICRO ((int)1e6)
#define ISN 0
#define INITIAL_TIMEOUT 1
#define ALPHA 0.125
#define BETA 0.25

void doNothing(__attribute__((unused)) int signum) {}

void updateRTTAndTimeout(int sampleRTT, int *estimatedRTTPtr, int *devRTTPtr, int *timeoutPtr, float alpha, float beta) {
    assert(sampleRTT >= 0);

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
    if(sigaction(SIGALRM, &sa, NULL) != 0) {
        perror("failed to set sigaction");
        close(clientSocket);
        fclose(file);
        exit(1);
    }

    struct TCPSegment clientSegment, serverSegment;
    ssize_t serverSegmentLen;
    int timeout = (int)(INITIAL_TIMEOUT * SI_MICRO);
    int isSampleRTTBeingMeasured;
    int timeRemaining;
    int estimatedRTT = -1;
    int devRTT;

    clientSegment = makeTCPSegment(ackPort, udplPort, ISN, 0, SYN_FLAG, NULL, 0);

    fprintf(stderr, "log: sending SYN\n");
    isSampleRTTBeingMeasured = 1;
    for(;;) {
        if(sendto(clientSocket, &clientSegment, HEADER_LEN, 0, (struct sockaddr *)&udplAddr, sizeof(udplAddr)) != HEADER_LEN) {
            perror("failed to send to socket");
            close(clientSocket);
            fclose(file);
            exit(1);
        }

        errno = 0;
        ualarm(timeout, 0);
        serverSegmentLen = recvfrom(clientSocket, &serverSegment, sizeof(serverSegment), 0, NULL, NULL);
        timeRemaining = (int)ualarm(0, 0);
        if(errno == EINTR) {
            fprintf(stderr, "warning: failed to receive SYNACK\n");
            isSampleRTTBeingMeasured = 0;
            timeout *= 2;
            continue;
        }
        if(serverSegmentLen < 0) {
            perror("failed to read from socket");
            close(clientSocket);
            fclose(file);
            exit(1);
        }

        if(isChecksumValid(serverSegment.header)) {
            assert(serverSegment.header.ackNum == ISN + 1 && isFlagSet(serverSegment.header, SYN_FLAG | ACK_FLAG));
            if(isSampleRTTBeingMeasured) {
                updateRTTAndTimeout(timeout - timeRemaining, &estimatedRTT, &devRTT, &timeout, ALPHA, BETA);
            }
            break;
        }
    }

    uint32_t nextExpectedServerSeq = serverSegment.header.seqNum + 1;

    fprintf(stderr, "log: received SYNACK, sending ACK\n");
    clientSegment = makeTCPSegment(ackPort, udplPort, ISN + 1, nextExpectedServerSeq, ACK_FLAG, NULL, 0);
    if(sendto(clientSocket, &clientSegment, HEADER_LEN, 0, (struct sockaddr *)&udplAddr, sizeof(udplAddr)) != HEADER_LEN) {
        perror("failed to send to socket");
        close(clientSocket);
        fclose(file);
        exit(1);
    }

    uint32_t seqNum = ISN + 2;
    clock_t startTime;
    uint32_t seqNumBeingTimed;
    isSampleRTTBeingMeasured = 0;

    int windowCapacity = windowSize / MSS;
    struct Window *window = newWindow(windowCapacity);
    if(!window) {
        perror("failed to malloc\n");
        close(clientSocket);
        fclose(file);
        exit(1);
    }
    struct TCPSegment fileSegment;
    int fileSegmentLen;
    char fileBuffer[MSS];
    size_t fileBufferLen;

    fprintf(stderr, "log: sending file\n");
    int remainingTimeout = timeout;
    do {
        while(!isFull(window) && (fileBufferLen = fread(fileBuffer, 1, MSS, file)) > 0) {
            if(!isSampleRTTBeingMeasured) {
                isSampleRTTBeingMeasured = 1;
                seqNumBeingTimed = seqNum;
                startTime = clock();
            }
            fileSegment = makeTCPSegment(ackPort, udplPort, seqNum, nextExpectedServerSeq, 0, fileBuffer, (int)fileBufferLen);
            fileSegmentLen = HEADER_LEN + fileSegment.dataLen;
            offer(window, fileSegment);
            seqNum += fileBufferLen;

            if(sendto(clientSocket, &fileSegment, fileSegmentLen, 0, (struct sockaddr *)&udplAddr, sizeof(udplAddr)) != fileSegmentLen) {
                perror("failed to send to socket");
                close(clientSocket);
                fclose(file);
                freeWindow(window);
                exit(1);
            }
        }
        if(!fileBufferLen && ferror(file)) {
            perror("failed to read file");
            close(clientSocket);
            fclose(file);
            freeWindow(window);
            exit(1);
        }

        errno = 0;
        ualarm(remainingTimeout, 0);
        serverSegmentLen = recvfrom(clientSocket, &serverSegment, sizeof(serverSegment), 0, NULL, NULL);
        timeRemaining = (int)ualarm(0, 0);
        if(errno == EINTR) {
            timeout *= 2;
            remainingTimeout = timeout;

            fileSegment = window->arr[window->startIndex];
            fileSegmentLen = HEADER_LEN + fileSegment.dataLen;

            fprintf(stderr, "warning: failed to receive ACK for seq %d\n", fileSegment.header.seqNum);
            if(sendto(clientSocket, &fileSegment, fileSegmentLen, 0, (struct sockaddr *)&udplAddr, sizeof(udplAddr)) != fileSegmentLen) {
                perror("failed to send to socket");
                close(clientSocket);
                fclose(file);
                freeWindow(window);
                exit(1);
            }
            if(fileSegment.header.seqNum == seqNumBeingTimed) {
                isSampleRTTBeingMeasured = 0;
            }
            continue;
        }
        if(serverSegmentLen < 0) {
            perror("failed to read from socket");
            close(clientSocket);
            fclose(file);
            freeWindow(window);
            exit(1);
        }

        int resumeTimer = 1;
        if(isChecksumValid(serverSegment.header)) {
            uint32_t serverACKNum = serverSegment.header.ackNum;
            if(isSeqNumInRange(window, serverACKNum, 1)) {
                assert(isFlagSet(serverSegment.header, ACK_FLAG));
                for( ; !isEmpty(window) && window->arr[window->startIndex].header.seqNum != serverACKNum; deleteHead(window));
                // isEmpty(window) || window->arr[window->startIndex].header.seqNum == serverACKNum

                if(isSampleRTTBeingMeasured && !isSeqNumInRange(window, seqNumBeingTimed, 0)) {
                    updateRTTAndTimeout((int)((clock() - startTime) / CLOCKS_PER_SEC * SI_MICRO), &estimatedRTT, &devRTT, &timeout, ALPHA, BETA);
                }

                isSampleRTTBeingMeasured = 0;
                remainingTimeout = timeout;
                resumeTimer = 0;
            }
            else if(serverACKNum == ISN + 1) {
                assert(isFlagSet(serverSegment.header, SYN_FLAG | ACK_FLAG));
                if(sendto(clientSocket, &clientSegment, HEADER_LEN, 0, (struct sockaddr *)&udplAddr, sizeof(udplAddr)) != HEADER_LEN) {
                    perror("failed to send to socket");
                    close(clientSocket);
                    fclose(file);
                    exit(1);
                }
            }
            // else ACK out of range
        }
        if(resumeTimer) {
            remainingTimeout = timeRemaining;
        }
    } while(!isEmpty(window));

    // TODO
    clientSegment = makeTCPSegment(ackPort, udplPort, seqNum++, nextExpectedServerSeq, FIN_FLAG, NULL, 0);
    assert(sendto(clientSocket, &clientSegment, HEADER_LEN, 0, (struct sockaddr *)&udplAddr, sizeof(udplAddr)) == HEADER_LEN);

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
