#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "tcp.h"
#include "validators.h"

#define SI_MICRO ((int)1e6)
#define ISN 0
#define INITIAL_TIMEOUT 1
#define ALPHA 0.125
#define BETA 0.25
// #define FINAL_WAIT 10

void doNothing(int signum) {}

void runServer(char *fileStr, int listenPort, char *ackAddress, int ackPort) {
    int serverSocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(serverSocket < 0) {
        perror("failed to create socket");
        exit(1);
    }

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(listenPort);
    if(bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("failed to bind port");
        close(serverSocket);
        exit(1);
    }

    struct sockaddr_in ackAddr;
    memset(&ackAddr, 0, sizeof(ackAddr));
    ackAddr.sin_family = AF_INET;
    ackAddr.sin_addr.s_addr = inet_addr(ackAddress);
    ackAddr.sin_port = htons(ackPort);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &doNothing;
    if(sigaction(SIGALRM, &sa, NULL) != 0) {
        perror("failed to set sigaction");
        close(serverSocket);
        exit(1);
    }

    struct TCPSegment *serverSegment = (struct TCPSegment *)malloc(sizeof(struct TCPSegment));
    struct TCPSegment *clientSegment = (struct TCPSegment *)malloc(sizeof(struct TCPSegment));
    if(!serverSegment || !clientSegment) {
        perror("failed to malloc");
        free(serverSegment); free(clientSegment); close(serverSocket);
        exit(1);
    }
    ssize_t clientSegmentLen;
    uint32_t nextExpectedClientSeq;
    int timeout = (int)(INITIAL_TIMEOUT * 1e6);

    fprintf(stderr, "log: listening for SYN\n");
    for(;;) {
        clientSegmentLen = recvfrom(serverSocket, clientSegment, sizeof(struct TCPSegment), 0, NULL, NULL);
        if(clientSegmentLen < 0) {
            perror("failed to read from socket");
            free(serverSegment); free(clientSegment); close(serverSocket);
            exit(1);
        }

        if(isChecksumValid(clientSegment)) {
            assert(isFlagSet(clientSegment, SYN_FLAG));
            break;
        }
    }

    nextExpectedClientSeq = clientSegment->seqNum + 1;
    fillTCPSegment(serverSegment, listenPort, ackPort, ISN, nextExpectedClientSeq, SYN_FLAG | ACK_FLAG, NULL, 0);

    fprintf(stderr, "log: received SYN, sending SYNACK\n");
    for(;;) {
        if(sendto(serverSocket, serverSegment, HEADER_LEN, 0, (struct sockaddr *)&ackAddr, sizeof(ackAddr)) != HEADER_LEN) {
            perror("failed to send to socket");
            free(serverSegment); free(clientSegment); close(serverSocket);
            exit(1);
        }

        errno = 0;
        ualarm(timeout, 0);
        clientSegmentLen = recvfrom(serverSocket, clientSegment, sizeof(struct TCPSegment), 0, NULL, NULL);
        ualarm(0, 0);
        if(errno == EINTR) {
            fprintf(stderr, "warning: failed to receive ACK\n");
            timeout *= 2;
            continue;
        }
        if(clientSegmentLen < 0) {
            perror("failed to read from socket");
            free(serverSegment); free(clientSegment); close(serverSocket);
            exit(1);
        }

        if(isChecksumValid(clientSegment)) {
            assert(clientSegment->ackNum == nextExpectedClientSeq && isFlagSet(clientSegment, ACK_FLAG));
            break;
        }
    }

    nextExpectedClientSeq++;

    FILE *file = fopen(fileStr, "wb");
    if(!file) {
        perror("failed to open file");
        exit(1);
    }
    ssize_t clientDataLen;

    fprintf(stderr, "log: receiving file\n");
    for(;;) {
        if((clientSegmentLen = recvfrom(serverSocket, clientSegment, sizeof(struct TCPSegment), 0, NULL, NULL)) < 0) {
            perror("failed to read from socket");
            fclose(file); free(serverSegment); free(clientSegment); close(serverSocket);
            exit(1);
        }
        if(isChecksumValid(clientSegment)) {
            if(clientSegment->seqNum == nextExpectedClientSeq) {
                if(isFlagSet(clientSegment, FIN_FLAG)) {
                    break;
                }
                clientDataLen = clientSegmentLen - HEADER_LEN;
                if(fwrite(clientSegment->data, 1, clientDataLen, file) != clientDataLen) {
                    perror("failed to write to file");
                    fclose(file); free(serverSegment); free(clientSegment); close(serverSocket);
                    exit(1);
                }
                nextExpectedClientSeq += clientDataLen;
            }
            else {
                fprintf(stderr, "warning: received out-of-order seq %d\n", clientSegment->seqNum);
            }

            fillTCPSegment(serverSegment, listenPort, ackPort, ISN + 1, nextExpectedClientSeq, ACK_FLAG, NULL, 0);
            if(sendto(serverSocket, serverSegment, HEADER_LEN, 0, (struct sockaddr *)&ackAddr, sizeof(ackAddr)) != HEADER_LEN) {
                perror("failed to send to socket");
                fclose(file); free(serverSegment); free(clientSegment); close(serverSocket);
                exit(1);
            }
        }
    }

    fclose(file);

    // nextExpectedClientSeq;
    fillTCPSegment(serverSegment, listenPort, ackPort, ISN + 1, nextExpectedClientSeq + 1, ACK_FLAG, NULL, 0);
    fprintf(stderr, "log: received FIN, sending ACK\n");
    if(sendto(serverSocket, serverSegment, HEADER_LEN, 0, (struct sockaddr *)&ackAddr, sizeof(ackAddr)) != HEADER_LEN) {
        perror("failed to send to socket");
        free(serverSegment); free(clientSegment); close(serverSocket);
        exit(1);
    }

    struct TCPSegment *finSegment = (struct TCPSegment *)malloc(sizeof(struct TCPSegment));
    fillTCPSegment(finSegment, listenPort, ackPort, ISN + 1, nextExpectedClientSeq + 1, FIN_FLAG, NULL, 0);
    int remainingTimeout = timeout;
    int timeRemaining;
    fprintf(stderr, "log: sending FIN\n");
    for(;;) {
        if(sendto(serverSocket, finSegment, HEADER_LEN, 0, (struct sockaddr *)&ackAddr, sizeof(ackAddr)) != HEADER_LEN) {
            perror("failed to send to socket");
            free(finSegment); free(serverSegment); free(clientSegment); close(serverSocket);
            exit(1);
        }

        errno = 0;
        ualarm(remainingTimeout, 0);
        clientSegmentLen = recvfrom(serverSocket, clientSegment, sizeof(struct TCPSegment), 0, NULL, NULL);
        timeRemaining = (int)ualarm(0, 0);
        if(errno == EINTR) {
            fprintf(stderr, "warning: failed to receive ACK\n");
            timeout *= 2;
            remainingTimeout = timeout;
            continue;
        }
        if(clientSegmentLen < 0) {
            perror("failed to read from socket");
            free(finSegment); free(serverSegment); free(clientSegment); close(serverSocket);
            exit(1);
        }

        if(isChecksumValid(clientSegment)) {
            if(clientSegment->ackNum == ISN + 2 && isFlagSet(clientSegment, ACK_FLAG)) {
                break;
            }
            if(clientSegment->seqNum == nextExpectedClientSeq && isFlagSet(clientSegment, FIN_FLAG)) {
                if(sendto(serverSocket, serverSegment, HEADER_LEN, 0, (struct sockaddr *)&ackAddr, sizeof(ackAddr)) != HEADER_LEN) {
                    perror("failed to send to socket");
                    free(finSegment); free(serverSegment); free(clientSegment); close(serverSocket);
                    exit(1);
                }
            }
        }
        remainingTimeout = timeRemaining;
    }

    free(finSegment); free(serverSegment); free(clientSegment); close(serverSocket);
    fprintf(stderr, "log: goodbye\n");
}

int main(int argc, char **argv) {
    if(argc != 5) {
        fprintf(stderr, "usage: tcpserver <file> <listening port> <ack address> <ack port>\n");
        exit(1);
    }

    char *fileStr = argv[1];
    int listenPort = getPort(argv[2]);
    if(!listenPort) {
        fprintf(stderr, "error: invalid listening port\n");
        exit(1);
    }
    char *ackAddress = argv[3];
    if(!isValidIP(ackAddress)) {
        fprintf(stderr, "error: invalid ack address\n");
        exit(1);
    }
    int ackPort = getPort(argv[4]);
    if(!ackPort) {
        fprintf(stderr, "error: invalid ack port\n");
        exit(1);
    }

    runServer(fileStr, listenPort, ackAddress, ackPort);
}
