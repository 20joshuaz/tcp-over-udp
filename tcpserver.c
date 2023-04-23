#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "helpers.h"

#define BUFFER_SIZE 1000
#define INITIAL_TIMEOUT 3

void runServer(char *file, int listenPort, char *ackAddress, int ackPort) {
    int serverSocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(serverSocket < 0) {
        printf("error: failed to create socket\n");
        return;
    }

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(listenPort);
    if(bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        printf("error: failed to bind port\n");
        return;
    }

    struct sockaddr_in ackAddr;
    memset(&ackAddr, 0, sizeof(ackAddr));
    ackAddr.sin_family = AF_INET;
    ackAddr.sin_addr.s_addr = inet_addr(ackAddress);
    ackAddr.sin_port = htons(ackPort);

    DO_NOTHING_ON_ALARM

    struct TCPSegment segment;
    int seqNum = 0;
    int clientSeqNum;
    char clientMsg[BUFFER_SIZE];
    int clientMsgLen;

    printf("log: listening for connection requests\n");
    do {
        clientMsgLen = (int)recvfrom(serverSocket, clientMsg, BUFFER_SIZE, 0, NULL, NULL);
        if(clientMsgLen < 0) {
            printf("error: failed to read from socket\n");
            return;
        }
        assert(clientMsgLen == HEADER_LEN);
        segment = parseTCPSegment(clientMsg, clientMsgLen);
    } while(!doesChecksumAgree(segment.header) && isSYNSet(segment.header));

    printf("log: received connection request, sending SYNACK\n");
    clientSeqNum = (int)segment.header.seqNum;
    segment = createTCPSegment(listenPort, ackPort, seqNum, clientSeqNum + 1, 1, 1, 0, NULL, 0);
    do {
        if(sendto(serverSocket, &segment, HEADER_LEN, 0, (struct sockaddr *)&ackAddr, sizeof(ackAddr)) != HEADER_LEN) {
            printf("error: failed to send to socket\n");
            return;
        }

        errno = 0;
        alarm(INITIAL_TIMEOUT);
        clientMsgLen = (int)recvfrom(serverSocket, clientMsg, BUFFER_SIZE, 0, NULL, NULL);
        alarm(0);
        if(errno == EINTR) {
            printf("warning: failed to receive ACK\n");
            continue;
        }
        if(clientMsgLen < 0) {
            printf("error: failed to read from socket\n");
            return;
        }
        segment = parseTCPSegment(clientMsg, clientMsgLen);
    } while(doesChecksumAgree(segment.header) && isACKSet(segment.header));

    printf("Success\n");
    close(serverSocket);
}

int main(int argc, char **argv) {
    if(argc != 5) {
        printf("usage: tcpserver <file> <listening port> <ack address> <ack port>\n");
        return 0;
    }

    char *file = argv[1];
    int listenPort = getPort(argv[2]);
    if(!listenPort) {
        printf("error: invalid listening port\n");
        return 0;
    }
    char *ackAddress = argv[3];
    if(!isValidIP(ackAddress)) {
        printf("error: invalid ack address\n");
        return 0;
    }
    int ackPort = getPort(argv[4]);
    if(!ackPort) {
        printf("error: invalid ack port\n");
        return 0;
    }

    runServer(file, listenPort, ackAddress, ackPort);
}
