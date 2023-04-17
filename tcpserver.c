#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "helpers.h"

void runServer(char *file, int listenPort, char *ackAddress, int ackPort) {
    int serverSocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (serverSocket < 0) {
        printf("error: failed to create socket\n");
        return;
    }

    struct sockaddr_in servAddr;
    memset(&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servAddr.sin_port = htons(listenPort);

    if (bind(serverSocket, (struct sockaddr *)&servAddr, sizeof(servAddr)) < 0) {
        printf("error: failed to bind port\n");
        return;
    }

    char clientMsg[BUFFER_SIZE];
    ssize_t clientMsgLen;
    if ((clientMsgLen = recvfrom(serverSocket, clientMsg, BUFFER_SIZE, 0, NULL, NULL)) < 0) {
        printf("error: failed to receive\n");
        return;
    }

    assert(clientMsgLen == HEADER_LEN);


    close(serverSocket);
}

int main(int argc, char **argv) {
    if (argc != 5) {
        printf("usage: tcpserver <file> <listening port> <address for acks> <port for acks>\n");
        return 0;
    }

    char *file = argv[1];
    int listenPort = getPort(argv[2]);
    if (!listenPort) {
        printf("error: invalid listening port\n");
        return 0;
    }
    char *ackAddress = argv[3];
    if (!isValidIP(ackAddress)) {
        printf("error: invalid address for acks\n");
        return 0;
    }
    int ackPort = getPort(argv[4]);
    if (!ackPort) {
        printf("error: invalid port for acks\n");
        return 0;
    }

    runServer(file, listenPort, ackAddress, ackPort);
}
