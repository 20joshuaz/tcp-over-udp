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

void runServer(char *file, int listenPort, char *ackAddress, int ackPort) {
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
        exit(1);
    }

    struct sockaddr_in ackAddr;
    memset(&ackAddr, 0, sizeof(ackAddr));
    ackAddr.sin_family = AF_INET;
    ackAddr.sin_addr.s_addr = inet_addr(ackAddress);
    ackAddr.sin_port = htons(ackPort);

    DO_NOTHING_ON_ALARM

    struct TCPSegment segment;
    uint32_t seqNum = 0;
    uint32_t clientACKNum;
    int timeout = INITIAL_TIMEOUT * 1e6;
    char clientMsg[BUFFER_SIZE];
    int clientMsgLen;

    printf("log: listening for connection requests\n");
    for(;;) {
        clientMsgLen = (int)recvfrom(serverSocket, clientMsg, BUFFER_SIZE, 0, NULL, NULL);
        if(clientMsgLen < 0) {
            perror("failed to read from socket");
            exit(1);
        }
        assert(clientMsgLen == HEADER_LEN);
        segment = parseTCPSegment(clientMsg);
        if(doesChecksumAgree(segment.header) && isFlagSet(segment.header, SYN_FLAG)) {
            break;
        }
    }

    clientACKNum = (int)segment.header.seqNum + 1;
    segment = makeTCPSegment(listenPort, ackPort, seqNum++, clientACKNum, SYN_FLAG | ACK_FLAG, NULL, 0);

    printf("log: received connection request, sending SYNACK\n");
    for(;;) {
        if(sendto(serverSocket, &segment, segment.length, 0, (struct sockaddr *)&ackAddr, sizeof(ackAddr)) != HEADER_LEN) {
            perror("failed to send to socket");
            exit(1);
        }

        errno = 0;
        ualarm(timeout, 0);
        clientMsgLen = (int)recvfrom(serverSocket, clientMsg, BUFFER_SIZE, 0, NULL, NULL);
        ualarm(0, 0);
        if(errno == EINTR) {
            printf("warning: failed to receive ACK\n");
            timeout *= 2;
            continue;
        }
        if(clientMsgLen < 0) {
            perror("failed to read from socket");
            exit(1);
        }
        segment = parseTCPSegment(clientMsg);
        if(doesChecksumAgree(segment.header) && segment.header.ackNum == seqNum && isFlagSet(segment.header, ACK_FLAG)) {
            break;
        }
    }

    printf("Success\n");
    close(serverSocket);
}

int main(int argc, char **argv) {
    if(argc != 5) {
        fprintf(stderr, "usage: tcpserver <file> <listening port> <ack address> <ack port>");
        exit(1);
    }

    char *file = argv[1];
    int listenPort = getPort(argv[2]);
    if(!listenPort) {
        fprintf(stderr, "error: invalid listening port");
        exit(1);
    }
    char *ackAddress = argv[3];
    if(!isValidIP(ackAddress)) {
        fprintf(stderr, "error: invalid ack address");
        exit(1);
    }
    int ackPort = getPort(argv[4]);
    if(!ackPort) {
        fprintf(stderr, "error: invalid ack port");
        exit(1);
    }

    runServer(file, listenPort, ackAddress, ackPort);
}
