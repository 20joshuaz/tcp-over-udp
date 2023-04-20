#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "helpers.h"

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

    struct sockaddr_in serverAddr;
    memset(&clientAddr, 0, sizeof(clientAddr));
    clientAddr.sin_family = AF_INET;
    clientAddr.sin_addr.s_addr = inet_addr(udplAddress);
    clientAddr.sin_port = htons(udplPort);

    struct TCPSegment segment;
    fd_set fds;
    FD_ZERO(&fds);
    struct timeval tv = {5, 0};
    int seqNum = 0;
    int ackNum;

    segment = createTCPSegment(ackPort, udplPort, seqNum, 0, 0, 1, 0, NULL, 0);
    if(sendto(clientSocket, &segment, HEADER_LEN, 0, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) != HEADER_LEN) {
        printf("error: failed to send SYN\n");
        return;
    }

    FD_SET(clientSocket, &fds);
}

int main(int argc, char **argv) {
    if(argc != 6) {
        printf("usage: tcpclient <file> <address of udpl> <port of udpl> <window size> <ack port>\n");
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
