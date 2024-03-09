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

#include "tcp.h"
#include "helpers.h"

#define ISN 0
#define INITIAL_TIMEOUT 1  // the initial timeout, in seconds
#define TIMEOUT_MULTIPLIER 1.1  // the timeout multiplier when a timeout occurs
#define ALPHA 0.125
#define BETA 0.25

void doNothing(int signum) {}

void runServer(char *fileStr, int listenPort, char *ackAddress, int ackPort)
{
	// Create socket
	int serverSocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (serverSocket < 0) {
		perror("failed to create socket");
		exit(1);
	}

	// Bind socket to listenPort
	struct sockaddr_in serverAddr;
	memset(&serverAddr, 0, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddr.sin_port = htons(listenPort);
	if (bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
		perror("failed to bind port");
		close(serverSocket);
		exit(1);
	}

	struct sockaddr_in ackAddr;  // address for sending ACKs
	memset(&ackAddr, 0, sizeof(ackAddr));
	ackAddr.sin_family = AF_INET;
	ackAddr.sin_addr.s_addr = inet_addr(ackAddress);
	ackAddr.sin_port = htons(ackPort);

	// Do nothing on alarm signal
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = &doNothing;
	if (sigaction(SIGALRM, &sa, NULL) != 0) {
		perror("failed to set sigaction");
		close(serverSocket);
		exit(1);
	}

	// serverSegment holds segments created by the server. clientSegment holds segments received from the client.
	struct TCPSegment *serverSegment = malloc(sizeof(struct TCPSegment));
	struct TCPSegment *clientSegment = malloc(sizeof(struct TCPSegment));
	if (!serverSegment || !clientSegment) {
		perror("failed to malloc");
		free(serverSegment); free(clientSegment); close(serverSocket);
		exit(1);
	}
	ssize_t clientSegmentLen;  // amount of data in clientSegment (including TCP header)
	uint32_t nextExpectedClientSeq;  // the next seq expected to be sent by the client (i.e., the ACK sent back to the client)
	int timeout = INITIAL_TIMEOUT * SI_MICRO;  // transmission timeout

	struct itimerval itTimeout;
	memset(&itTimeout, 0, sizeof(itTimeout));
	setMicroTime(&itTimeout, timeout);
	struct itimerval disarmer;
	memset(&disarmer, 0, sizeof(disarmer));

	/*
	 * Listen for SYN:
	 *  - Call recvfrom.
	 *  - When segment is received, check that it is not corrupted and the SYN flag is set. If so, break from loop;
	 *    else, repeat.
	 */
	fprintf(stderr, "log: listening for SYN\n");
	for (;;) {
		clientSegmentLen = recvfrom(serverSocket, clientSegment, sizeof(struct TCPSegment), 0, NULL, NULL);
		if (clientSegmentLen < 0) {
			perror("failed to read from socket");
			free(serverSegment); free(clientSegment); close(serverSocket);
			exit(1);
		}

		if (isChecksumValid(clientSegment) && isFlagSet(clientSegment, SYN_FLAG)) {
			break;
		}
	}

	// Get client's ISN from segment
	nextExpectedClientSeq = clientSegment->seqNum + 1;

	// Create SYNACK segment
	fillTCPSegment(serverSegment, listenPort, ackPort, ISN, nextExpectedClientSeq, SYN_FLAG | ACK_FLAG, NULL, 0);

	/*
	 * Send SYNACK and listen for ACK:
	 *  - Send SYNACK.
	 *  - Call recvfrom. If nothing is received within the timeout, increase it and repeat.
	 *  - If a segment is received, check that is it not corrupted, the ACK is ISN + 1, and the ACK flag is set. If so, break from loop;
	 *    else, repeat.
	 */
	fprintf(stderr, "log: received SYN, sending SYNACK and listening for ACK\n");
	for (;;) {
		if (sendto(serverSocket, serverSegment, HEADER_LEN, 0,
			(struct sockaddr *)&ackAddr, sizeof(ackAddr)) != HEADER_LEN) {
			perror("failed to send to socket");
			free(serverSegment); free(clientSegment); close(serverSocket);
			exit(1);
		}

		errno = 0;
		setitimer(ITIMER_REAL, &itTimeout, NULL);
		clientSegmentLen = recvfrom(serverSocket, clientSegment, sizeof(struct TCPSegment), 0, NULL, NULL);
		setitimer(ITIMER_REAL, &disarmer, NULL);
		if (errno == EINTR) {
			fprintf(stderr, "warning: failed to receive ACK for SYNACK\n");
			timeout = (int)(timeout * TIMEOUT_MULTIPLIER);
			setMicroTime(&itTimeout, timeout);
			continue;
		}
		if (clientSegmentLen < 0) {
			perror("failed to read from socket");
			free(serverSegment); free(clientSegment); close(serverSocket);
			exit(1);
		}

		if (isChecksumValid(clientSegment) && clientSegment->ackNum == ISN + 1
			&& isFlagSet(clientSegment, ACK_FLAG)) {
			break;
		}
	}

	nextExpectedClientSeq++;

	// Open file for writing
	FILE *file = fopen(fileStr, "wb");
	if (!file) {
		perror("failed to open file");
		exit(1);
	}
	ssize_t clientDataLen;  // amount of data excluding the TCP header
	uint32_t bytesReceived = 0;  // the number of bytes received, used for logging

	/*
	 * Receive file:
	 *  - The client sends the file, so all the server has to do is listen.
	 *  - When a segment is received, check if it is corrupted. If it is, then ignore it.
	 *  - Else, check if the FIN flag is set. If so, break from loop.
	 *  - Else, check the segment's seq. If the seq is the next expected one, write to the file and update the next expected seq.
	 *  - Regardless if the seq is the next expected one, send an ACK to the client specifying the next expected seq.
	 */
	fprintf(stderr, "log: receiving file\n");
	for (;;) {
		if ((clientSegmentLen = recvfrom(serverSocket, clientSegment,
			sizeof(struct TCPSegment), 0, NULL, NULL)) < 0) {
			perror("failed to read from socket");
			fclose(file); free(serverSegment); free(clientSegment); close(serverSocket);
			exit(1);
		}
		if (isChecksumValid(clientSegment)) {
			if (clientSegment->seqNum == nextExpectedClientSeq) {
				if (isFlagSet(clientSegment, FIN_FLAG)) {
					break;
				}

				clientDataLen = clientSegmentLen - HEADER_LEN;
				fprintf(stderr, "log: received %d bytes\r", (bytesReceived += clientDataLen));
				if (fwrite(clientSegment->data, 1, clientDataLen, file) != clientDataLen) {
					perror("failed to write to file");
					fclose(file); free(serverSegment); free(clientSegment); close(serverSocket);
					exit(1);
				}
				nextExpectedClientSeq += clientDataLen;
			}

			fillTCPSegment(serverSegment, listenPort, ackPort, ISN + 1, nextExpectedClientSeq, ACK_FLAG, NULL, 0);
			if (sendto(serverSocket, serverSegment, HEADER_LEN, 0,
				(struct sockaddr *)&ackAddr, sizeof(ackAddr)) != HEADER_LEN) {
				perror("failed to send to socket");
				fclose(file); free(serverSegment); free(clientSegment); close(serverSocket);
				exit(1);
			}
		}
	}

	fprintf(stderr, "\n");
	fclose(file);

	// Create and send ACK for client's FIN
	fillTCPSegment(serverSegment, listenPort, ackPort, ISN + 1, nextExpectedClientSeq + 1, ACK_FLAG, NULL, 0);
	fprintf(stderr, "log: received FIN, sending ACK\n");
	if (sendto(serverSocket, serverSegment, HEADER_LEN, 0,
		(struct sockaddr *)&ackAddr, sizeof(ackAddr)) != HEADER_LEN) {
		perror("failed to send to socket");
		free(serverSegment); free(clientSegment); close(serverSocket);
		exit(1);
	}

	// Create FIN segment
	struct TCPSegment *finSegment = malloc(sizeof(struct TCPSegment));
	fillTCPSegment(finSegment, listenPort, ackPort, ISN + 1, nextExpectedClientSeq + 1, FIN_FLAG, NULL, 0);

	struct itimerval itRemainingTimeout;  // the remaining time in a timer, used to continue the timer
	memset(&itRemainingTimeout, 0, sizeof(itRemainingTimeout));
	setMicroTime(&itRemainingTimeout, timeout);

	/*
	 * Send FIN:
	 *  - Send FIN.
	 *  - Call recvfrom. If nothing is received within the timeout, increase it and repeat.
	 *  - If a segment is received, check that it is not corrupted. If it is, then ignore it.
	 *  - Else, check for two cases:
	 *    - If the ACK is ISN + 1 and the ACK flag is set, then break from loop.
	 *    - If the seq is the next expected one and the FIN flag is sent, then resend ACK.
	 *    - Else, the segment is a duplicate, so ignore.
	 *  - Repeat.
	 */
	fprintf(stderr, "log: sending FIN\n");
	for (;;) {
		if (sendto(serverSocket, finSegment, HEADER_LEN, 0,
			(struct sockaddr *)&ackAddr, sizeof(ackAddr)) != HEADER_LEN) {
			perror("failed to send to socket");
			free(finSegment); free(serverSegment); free(clientSegment); close(serverSocket);
			exit(1);
		}

		errno = 0;
		setitimer(ITIMER_REAL, &itRemainingTimeout, NULL);
		clientSegmentLen = recvfrom(serverSocket, clientSegment, sizeof(struct TCPSegment), 0, NULL, NULL);
		setitimer(ITIMER_REAL, &disarmer, &itRemainingTimeout);
		if (!getMicroTime(&itRemainingTimeout) || errno == EINTR) {
			fprintf(stderr, "warning: failed to receive ACK for FIN\n");
			timeout = (int)(timeout * TIMEOUT_MULTIPLIER);
			setMicroTime(&itRemainingTimeout, timeout);
			continue;
		}
		if (clientSegmentLen < 0) {
			perror("failed to read from socket");
			free(finSegment); free(serverSegment); free(clientSegment); close(serverSocket);
			exit(1);
		}

		if (isChecksumValid(clientSegment)) {
			if (clientSegment->ackNum == ISN + 2 && isFlagSet(clientSegment, ACK_FLAG)) {
				break;
			}
			if (clientSegment->seqNum == nextExpectedClientSeq && isFlagSet(clientSegment, FIN_FLAG)) {
				if (sendto(serverSocket, serverSegment, HEADER_LEN, 0,
					(struct sockaddr *)&ackAddr, sizeof(ackAddr)) != HEADER_LEN) {
					perror("failed to send to socket");
					free(finSegment); free(serverSegment); free(clientSegment); close(serverSocket);
					exit(1);
				}
			}
		}
	}

	free(finSegment); free(serverSegment); free(clientSegment); close(serverSocket);
	fprintf(stderr, "log: goodbye\n");
}

int main(int argc, char **argv)
{
	if (argc != 5) {
		fprintf(stderr, "usage: tcpserver <file> <listening port> <ack address> <ack port>\n");
		exit(1);
	}

	char *fileStr = argv[1];
	int listenPort = getPort(argv[2]);
	if (!listenPort) {
		fprintf(stderr, "error: invalid listening port\n");
		exit(1);
	}
	char *ackAddress = argv[3];
	if (!isValidIP(ackAddress)) {
		fprintf(stderr, "error: invalid ack address\n");
		exit(1);
	}
	int ackPort = getPort(argv[4]);
	if (!ackPort) {
		fprintf(stderr, "error: invalid ack port\n");
		exit(1);
	}

	runServer(fileStr, listenPort, ackAddress, ackPort);
}
