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

#define ISN 0
#define INITIAL_TIMEOUT 1
#define ALPHA 0.125
#define BETA 0.25

void doNothing(__attribute__((unused)) int signum) {}

void runServer(FILE *file, int listenPort, char *ackAddress, int ackPort) {
    int serverSocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(serverSocket < 0) {
        perror("failed to create socket");
        fclose(file);
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
        fclose(file);
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
        fclose(file);
        exit(1);
    }

    struct TCPSegment serverSegment, clientSegment;
    ssize_t clientSegmentLen;
    uint32_t nextExpectedClientSeq;
    int timeout = (int)(INITIAL_TIMEOUT * 1e6);

    fprintf(stderr, "log: listening for SYN\n");
    for(;;) {
        clientSegmentLen = recvfrom(serverSocket, &clientSegment, sizeof(clientSegment), 0, NULL, NULL);
        if(clientSegmentLen < 0) {
            perror("failed to read from socket");
            close(serverSocket);
            fclose(file);
            exit(1);
        }
        if(isChecksumValid(clientSegment.header)) {
            assert(isFlagSet(clientSegment.header, SYN_FLAG));
            break;
        }
    }

    nextExpectedClientSeq = clientSegment.header.seqNum + 1;
    serverSegment = makeTCPSegment(listenPort, ackPort, ISN, nextExpectedClientSeq, SYN_FLAG | ACK_FLAG, NULL, 0);

    fprintf(stderr, "log: received SYN, sending SYNACK\n");
    for(;;) {
        if(sendto(serverSocket, &serverSegment, HEADER_LEN, 0, (struct sockaddr *)&ackAddr, sizeof(ackAddr)) != HEADER_LEN) {
            perror("failed to send to socket");
            close(serverSocket);
            fclose(file);
            exit(1);
        }

        errno = 0;
        ualarm(timeout, 0);
        clientSegmentLen = recvfrom(serverSocket, &clientSegment, sizeof(clientSegment), 0, NULL, NULL);
        ualarm(0, 0);
        if(errno == EINTR) {
            fprintf(stderr, "warning: failed to receive ACK\n");
            timeout *= 2;
            continue;
        }
        if(clientSegmentLen < 0) {
            perror("failed to read from socket");
            close(serverSocket);
            fclose(file);
            exit(1);
        }
        if(isChecksumValid(clientSegment.header)) {
            assert(clientSegment.header.ackNum == nextExpectedClientSeq && isFlagSet(clientSegment.header, ACK_FLAG));
            break;
        }
    }

    nextExpectedClientSeq++;

    fprintf(stderr, "log: receiving file\n");
    ssize_t clientDataLen;
    for(;;) {
        if((clientSegmentLen = recvfrom(serverSocket, &clientSegment, sizeof(clientSegment), 0, NULL, NULL)) < 0) {
            perror("failed to read from socket");
            close(serverSocket);
            fclose(file);
            exit(1);
        }
        if(isChecksumValid(clientSegment.header)) {
            if(isFlagSet(clientSegment.header, FIN_FLAG)) {
                break;
            }

            if(clientSegment.header.seqNum == nextExpectedClientSeq) {
                clientDataLen = clientSegmentLen - HEADER_LEN;
                if(!fwrite(clientSegment.data, 1, clientDataLen, file) && ferror(file)) {
                    perror("failed to write to file");
                    close(serverSocket);
                    fclose(file);
                    exit(1);
                }

                nextExpectedClientSeq += clientDataLen;
            }
            else {
                fprintf(stderr, "warning: received out-of-order seq %d\n", clientSegment.header.seqNum);
            }

            serverSegment = makeTCPSegment(listenPort, ackPort, ISN + 1, nextExpectedClientSeq, ACK_FLAG, NULL, 0);
            if(sendto(serverSocket, &serverSegment, HEADER_LEN, 0, (struct sockaddr *)&ackAddr, sizeof(ackAddr)) != HEADER_LEN) {
                perror("failed to send to socket");
                close(serverSocket);
                fclose(file);
                exit(1);
            }
        }
    }

    close(serverSocket);
    fclose(file);
}

int main(int argc, char **argv) {
    if(argc != 5) {
        fprintf(stderr, "usage: tcpserver <file> <listening port> <ack address> <ack port>\n");
        exit(1);
    }

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

    char *fileStr = argv[1];
    FILE *file = fopen(fileStr, "wb");
    if(!file) {
        fprintf(stderr, "error: unable to write to file\n");
        exit(1);
    }

    runServer(file, listenPort, ackAddress, ackPort);
}
