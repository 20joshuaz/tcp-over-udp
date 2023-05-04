#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "window.h"
#include "tcp.h"
#include "validators.h"

#define SI_MICRO ((int)1e6)
#define ISN 0
#define INITIAL_TIMEOUT 1
#define TIMEOUT_MULTIPLIER 1.1
#define ALPHA 0.125
#define BETA 0.25
#define FINAL_WAIT 3

void doNothing(int signum) {}

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

void runClient(char *fileStr, char *udplAddress, int udplPort, int windowSize, int ackPort) {
    int clientSocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(clientSocket < 0) {
        perror("failed to create socket");
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
        exit(1);
    }

    struct TCPSegment *clientSegment = (struct TCPSegment *)malloc(sizeof(struct TCPSegment));
    struct TCPSegment *serverSegment = (struct TCPSegment *)malloc(sizeof(struct TCPSegment));
    if(!clientSegment || !serverSegment) {
        perror("failed to malloc");
        free(clientSegment); free(serverSegment); close(clientSocket);
        exit(1);
    }
    ssize_t serverSegmentLen;
    int timeout = (int)(INITIAL_TIMEOUT * SI_MICRO);
    int isSampleRTTBeingMeasured;
    int timeRemaining;
    int estimatedRTT = -1;
    int devRTT;

    fillTCPSegment(clientSegment, ackPort, udplPort, ISN, 0, SYN_FLAG, NULL, 0);

    isSampleRTTBeingMeasured = 1;
    fprintf(stderr, "log: sending SYN\n");
    for(;;) {
        if(sendto(clientSocket, clientSegment, HEADER_LEN, 0, (struct sockaddr *)&udplAddr, sizeof(udplAddr)) != HEADER_LEN) {
            perror("failed to send to socket");
            free(clientSegment); free(serverSegment); close(clientSocket);
            exit(1);
        }

        errno = 0;
        ualarm(timeout, 0);
        serverSegmentLen = recvfrom(clientSocket, serverSegment, sizeof(struct TCPSegment), 0, NULL, NULL);
        timeRemaining = (int)ualarm(0, 0);
        if(!timeRemaining || errno == EINTR) {
            fprintf(stderr, "warning: failed to receive SYNACK\n");
            isSampleRTTBeingMeasured = 0;
            timeout = (int)(timeout * TIMEOUT_MULTIPLIER);
            continue;
        }
        if(serverSegmentLen < 0) {
            perror("failed to read from socket");
            free(clientSegment); free(serverSegment); close(clientSocket);
            exit(1);
        }

        if(isChecksumValid(serverSegment) && serverSegment->ackNum == ISN + 1 && isFlagSet(serverSegment, SYN_FLAG | ACK_FLAG)) {
            if(isSampleRTTBeingMeasured) {
                updateRTTAndTimeout(timeout - timeRemaining, &estimatedRTT, &devRTT, &timeout, ALPHA, BETA);
            }
            break;
        }
    }

    uint32_t nextExpectedServerSeq = serverSegment->seqNum + 1;

    fillTCPSegment(clientSegment, ackPort, udplPort, ISN + 1, nextExpectedServerSeq, ACK_FLAG, NULL, 0);
    fprintf(stderr, "log: received SYNACK, sending ACK\n");
    if(sendto(clientSocket, clientSegment, HEADER_LEN, 0, (struct sockaddr *)&udplAddr, sizeof(udplAddr)) != HEADER_LEN) {
        perror("failed to send to socket");
        free(clientSegment); free(serverSegment); close(clientSocket);
        exit(1);
    }

    uint32_t seqNum = ISN + 2;
    struct timeval startTime, endTime, diffTime;
    uint32_t seqNumBeingTimed;
    isSampleRTTBeingMeasured = 0;

    FILE *file = fopen(fileStr, "rb");
    if(!file) {
        perror("failed to open file");
        free(clientSegment); free(serverSegment); close(clientSocket);
    }

    struct TCPSegment *fileSegment = (struct TCPSegment *)malloc(sizeof(struct TCPSegment));
    if(!fileSegment) {
        perror("failed to malloc");
        fclose(file); free(clientSegment); free(serverSegment); close(clientSocket);
        exit(1);
    }
    int fileSegmentLen;
    char fileBuffer[MSS];
    size_t fileBufferLen;
    struct Window *window = newWindow(windowSize / MSS);
    if(!window) {
        perror("failed to malloc");
        free(fileSegment); fclose(file); free(clientSegment); free(serverSegment); close(clientSocket);
        exit(1);
    }

    int remainingTimeout = timeout;
    fprintf(stderr, "log: sending file\n");
    do {
        while(!isFull(window) && (fileBufferLen = fread(fileBuffer, 1, MSS, file)) > 0) {
            fillTCPSegment(fileSegment, ackPort, udplPort, seqNum, nextExpectedServerSeq, 0, fileBuffer, (int)fileBufferLen);
            fileSegmentLen = HEADER_LEN + fileSegment->dataLen;
            offer(window, fileSegment);
            if(!isSampleRTTBeingMeasured) {
                isSampleRTTBeingMeasured = 1;
                seqNumBeingTimed = seqNum;
                gettimeofday(&startTime, NULL);
            }

            seqNum += fileSegment->dataLen;

            if(sendto(clientSocket, fileSegment, fileSegmentLen, 0, (struct sockaddr *)&udplAddr, sizeof(udplAddr)) != fileSegmentLen) {
                perror("failed to send to socket");
                freeWindow(window); free(fileSegment); fclose(file); free(clientSegment); free(serverSegment); close(clientSocket);
                exit(1);
            }
        }
        if(!fileBufferLen && ferror(file)) {
            perror("failed to read file");
            freeWindow(window); free(fileSegment); fclose(file); free(clientSegment); free(serverSegment); close(clientSocket);
            exit(1);
        }

        errno = 0;
        ualarm(remainingTimeout, 0);
        serverSegmentLen = recvfrom(clientSocket, serverSegment, sizeof(struct TCPSegment), 0, NULL, NULL);
        timeRemaining = (int)ualarm(0, 0);
        if(!timeRemaining || errno == EINTR) {
            timeout = (int)(timeout * TIMEOUT_MULTIPLIER);
            remainingTimeout = timeout;

            int currIndex = window->startIndex;
            struct TCPSegment *segmentInWindow;
            int segmentInWindowLen;
            do {
                segmentInWindow = window->arr + currIndex;
                segmentInWindowLen = HEADER_LEN + segmentInWindow->dataLen;
                if(sendto(clientSocket, segmentInWindow, segmentInWindowLen, 0, (struct sockaddr *)&udplAddr, sizeof(udplAddr)) != segmentInWindowLen) {
                    perror("failed to send to socket");
                    freeWindow(window); free(fileSegment); fclose(file); free(clientSegment); free(serverSegment); close(clientSocket);
                    exit(1);
                }
            } while((currIndex = next(window, currIndex)) != window->endIndex);
            isSampleRTTBeingMeasured = 0;
            continue;
        }
        if(serverSegmentLen < 0) {
            perror("failed to read from socket");
            freeWindow(window); free(fileSegment); fclose(file); free(clientSegment); free(serverSegment); close(clientSocket);
            exit(1);
        }

        int resumeTimer = 1;
        if(isChecksumValid(serverSegment)) {
            uint32_t serverACKNum = serverSegment->ackNum;
            if(serverACKNum > window->arr[window->startIndex].seqNum && isFlagSet(serverSegment, ACK_FLAG)) {
                for( ; !isEmpty(window) && window->arr[window->startIndex].seqNum != serverACKNum; deleteHead(window));
                // isEmpty(window) || window->arr[window->startIndex].seqNum == serverACKNum

                if(isSampleRTTBeingMeasured && !isEmpty(window) && seqNumBeingTimed < window->arr[window->startIndex].seqNum) {
                    gettimeofday(&endTime, NULL);
                    timersub(&endTime, &startTime, &diffTime);
                    updateRTTAndTimeout((int)(diffTime.tv_sec * SI_MICRO + diffTime.tv_usec), &estimatedRTT, &devRTT, &timeout, ALPHA, BETA);
                    isSampleRTTBeingMeasured = 0;
                }

                remainingTimeout = timeout;
                resumeTimer = 0;
            }
            else if(serverACKNum == ISN + 1 && isFlagSet(serverSegment, SYN_FLAG | ACK_FLAG)) {
                if(sendto(clientSocket, clientSegment, HEADER_LEN, 0, (struct sockaddr *)&udplAddr, sizeof(udplAddr)) != HEADER_LEN) {
                    perror("failed to send to socket");
                    freeWindow(window); free(fileSegment); fclose(file); free(clientSegment); free(serverSegment); close(clientSocket);
                    exit(1);
                }
            }
            // else ACK out of range
        }
        if(resumeTimer) {
            remainingTimeout = timeRemaining;
        }
    } while(!isEmpty(window));

    freeWindow(window);
    free(fileSegment);
    fclose(file);

    fillTCPSegment(clientSegment, ackPort, udplPort, seqNum++, nextExpectedServerSeq, FIN_FLAG, NULL, 0);
    fprintf(stderr, "log: finished sending file, sending FIN\n");
    for(;;) {
        if(sendto(clientSocket, clientSegment, HEADER_LEN, 0, (struct sockaddr *)&udplAddr, sizeof(udplAddr)) != HEADER_LEN) {
            perror("failed to send to socket");
            free(clientSegment); free(serverSegment); close(clientSocket);
            exit(1);
        }

        errno = 0;
        ualarm(timeout, 0);
        serverSegmentLen = recvfrom(clientSocket, serverSegment, sizeof(struct TCPSegment), 0, NULL, NULL);
        ualarm(0, 0);
        if(errno == EINTR) {
            fprintf(stderr, "warning: failed to receive ACK for FIN\n");
            timeout = (int)(timeout * TIMEOUT_MULTIPLIER);
            continue;
        }
        if(serverSegmentLen < 0) {
            perror("failed to read from socket");
            free(clientSegment); free(serverSegment); close(clientSocket);
            exit(1);
        }

        if(isChecksumValid(serverSegment) && serverSegment->ackNum == seqNum && isFlagSet(serverSegment, ACK_FLAG)) {
            break;
        }
    }

    fprintf(stderr, "log: received ACK for FIN, listening for FIN\n");
    for(;;) {
        serverSegmentLen = recvfrom(clientSocket, serverSegment, sizeof(struct TCPSegment), 0, NULL, NULL);
        if(serverSegmentLen < 0) {
            perror("failed to read from socket");
            free(clientSegment); free(serverSegment); close(clientSocket);
            exit(1);
        }

        if(isChecksumValid(serverSegment) && serverSegment->seqNum == nextExpectedServerSeq && isFlagSet(serverSegment, FIN_FLAG)) {
            break;
        }
    }

    fillTCPSegment(clientSegment, ackPort, udplPort, seqNum, nextExpectedServerSeq + 1, ACK_FLAG, NULL, 0);
    int hasSeenFIN = 1;
    remainingTimeout = (int)(FINAL_WAIT * SI_MICRO);
    fprintf(stderr, "log: received FIN, sending ACK and waiting %.1f seconds\n", (float)FINAL_WAIT);
    for(;;) {
        if(hasSeenFIN) {
            if(sendto(clientSocket, clientSegment, HEADER_LEN, 0, (struct sockaddr *)&udplAddr, sizeof(udplAddr)) != HEADER_LEN) {
                perror("failed to send to socket");
                free(clientSegment); free(serverSegment); close(clientSocket);
                exit(1);
            }
        }

        hasSeenFIN = 0;
        errno = 0;
        ualarm(remainingTimeout, 0);
        serverSegmentLen = recvfrom(clientSocket, serverSegment, sizeof(struct TCPSegment), 0, NULL, NULL);
        remainingTimeout = (int)ualarm(0, 0);
        if(errno == EINTR) {
            break;
        }
        if(serverSegmentLen < 0) {
            perror("failed to read from socket");
            free(clientSegment); free(serverSegment); close(clientSocket);
            exit(1);
        }

        if(isChecksumValid(serverSegment) && serverSegment->seqNum == nextExpectedServerSeq && isFlagSet(serverSegment, FIN_FLAG)) {
            hasSeenFIN = 1;
        }
    }

    free(clientSegment); free(serverSegment); close(clientSocket);
    fprintf(stderr, "log: goodbye\n");
}

int main(int argc, char **argv) {
    if(argc != 6) {
        fprintf(stderr, "usage: tcpclient <file> <address of udpl> <port of udpl> <window size> <ack port>\n");
        exit(1);
    }

    char *fileStr = argv[1];
    if(access(fileStr, F_OK) != 0) {
        perror("failed to access file");
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

    runClient(fileStr, udplAddress, udplPort, windowSize, ackPort);
}
