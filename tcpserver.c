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

#define BUFFER_SIZE 1000
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
    sigaction(SIGALRM, &sa, NULL);

    struct TCPSegment segment;
    uint32_t seqNum = 0;
    uint32_t nextExpectedClientSeq;
    int timeout = (int)(INITIAL_TIMEOUT * 1e6);
    char clientMsg[BUFFER_SIZE];
    int clientMsgLen;

    fprintf(stderr, "log: listening for SYN\n");
    for(;;) {
        clientMsgLen = (int)recvfrom(serverSocket, clientMsg, BUFFER_SIZE, 0, NULL, NULL);
        if(clientMsgLen < 0) {
            perror("failed to read from socket");
            close(serverSocket);
            fclose(file);
            exit(1);
        }
        assert(clientMsgLen == HEADER_LEN);
        segment = parseTCPSegment(clientMsg);
        if(isChecksumValid(segment.header) && isFlagSet(segment.header, SYN_FLAG)) {
            break;
        }
    }

    nextExpectedClientSeq = segment.header.seqNum + 1;
    segment = makeTCPSegment(listenPort, ackPort, seqNum++, nextExpectedClientSeq, SYN_FLAG | ACK_FLAG, NULL, 0);
    printTCPHeader(segment.header);

    fprintf(stderr, "log: received SYN, sending SYNACK\n");
    for(;;) {
        if(sendto(serverSocket, &segment, segment.length, 0, (struct sockaddr *)&ackAddr, sizeof(ackAddr)) != segment.length) {
            perror("failed to send to socket");
            close(serverSocket);
            fclose(file);
            exit(1);
        }

        errno = 0;
        ualarm(timeout, 0);
        clientMsgLen = (int)recvfrom(serverSocket, clientMsg, BUFFER_SIZE, 0, NULL, NULL);
        ualarm(0, 0);
        if(errno == EINTR) {
            fprintf(stderr, "warning: failed to receive ACK\n");
            timeout *= 2;
            continue;
        }
        if(clientMsgLen < 0) {
            perror("failed to read from socket");
            close(serverSocket);
            fclose(file);
            exit(1);
        }
        assert(clientMsgLen == HEADER_LEN);
        struct TCPSegment recvSegment = parseTCPSegment(clientMsg);
        printTCPHeader(recvSegment.header);
        if(isChecksumValid(recvSegment.header) && recvSegment.header.ackNum == segment.expectedACKNum && isFlagSet(recvSegment.header, ACK_FLAG)) {
            break;
        }
    }

    nextExpectedClientSeq++;

    fprintf(stderr, "log: receiving file\n");
    for(;;) {
        if((clientMsgLen = (int)recvfrom(serverSocket, clientMsg, BUFFER_SIZE, 0, NULL, NULL)) < 0) {
            perror("failed to read from socket");
            close(serverSocket);
            fclose(file);
            exit(1);
        }
        assert(clientMsgLen >= HEADER_LEN);
        segment = parseTCPSegment(clientMsg);
        if(isChecksumValid(segment.header) && segment.header.seqNum == nextExpectedClientSeq) {
            if(isFlagSet(segment.header, FIN_FLAG)) {
                break;
            }
            int clientDataLen = clientMsgLen - HEADER_LEN;
            size_t fileWriteLen = fwrite(segment.data, 1, clientDataLen, file);
            if(!fileWriteLen && ferror(file)) {
                perror("failed to write to file");
                close(serverSocket);
                fclose(file);
                exit(1);
            }

            nextExpectedClientSeq += clientDataLen;
            segment = makeTCPSegment(listenPort, ackPort, seqNum, nextExpectedClientSeq, ACK_FLAG, NULL, 0);
            if(sendto(serverSocket, &segment, segment.length, 0, (struct sockaddr *)&ackAddr, sizeof(ackAddr)) != segment.length) {
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
