#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "tcp.h"
#include "helpers.h"

#define ISN 0
#define INITIAL_TIMEOUT 1  // The initial timeout, in seconds
#define TIMEOUT_MULTIPLIER 1.1  // The timeout multiplier when a timeout occurs
#define ALPHA 0.125
#define BETA 0.25

int runServer(char *fileStr, int listenPort, char *ackAddress, int ackPort)
{
	// Create socket
	int serverSocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (serverSocket < 0) {
		perror("socket");
		exit(1);
	}

	// Bind socket to listenPort
	struct sockaddr_in serverAddr;
	memset(&serverAddr, 0, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddr.sin_port = htons(listenPort);
	if (bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
		perror("bind");
		goto fail;
	}

	struct sockaddr_in ackAddr;  // Address for sending ACKs
	memset(&ackAddr, 0, sizeof(ackAddr));
	ackAddr.sin_family = AF_INET;
	ackAddr.sin_addr.s_addr = inet_addr(ackAddress);
	ackAddr.sin_port = htons(ackPort);

	// serverSegment holds segments created by the server.
	// clientSegment holds segments received from the client.
	struct TCPSegment serverSegment, clientSegment;
	ssize_t clientSegmentLen;  // Amount of data in clientSegment (including TCP header)
	// The next seq expected to be sent by the client (i.e., the ACK sent back to the client)
	uint32_t nextExpectedClientSeq;
	/*
	 * Listen for SYN:
	 *  - Call recvfrom.
	 *  - When segment is received, check that it is not corrupted and the SYN flag is set.
	 *    If so, break from loop; else, repeat.
	 */
	fprintf(stderr, "log: listening for SYN\n");
	for (;;) {
		clientSegmentLen = recvfrom(serverSocket, &clientSegment,
			sizeof(struct TCPSegment), 0, NULL, NULL);
		if (clientSegmentLen < 0) {
			perror("recvfrom");
			goto fail;
		}

		convertTCPSegment(&clientSegment, 0);
		if (isChecksumValid(&clientSegment) && isFlagSet(&clientSegment, SYN_FLAG)) {
			break;
		}
	}

	// Get client's ISN from segment
	nextExpectedClientSeq = clientSegment.seqNum + 1;

	// Create SYNACK segment
	fillTCPSegment(&serverSegment, listenPort, ackPort, ISN,
		nextExpectedClientSeq, SYN_FLAG | ACK_FLAG, NULL, 0);
	convertTCPSegment(&serverSegment, 1);

	int timeoutMicros = INITIAL_TIMEOUT * SI_MICRO;  // transmission timeout
	int timeRemaining = timeoutMicros;
	int timeElapsed;
	struct timeval timeout, startTime, endTime;
	fd_set readFds;
	int fdsReady;

	/*
	 * Send SYNACK and listen for ACK:
	 *  - Send SYNACK.
	 *  - Call recvfrom. If nothing is received within the timeout, increase it and repeat.
	 *  - If a segment is received, check that is it not corrupted, the ACK is ISN + 1,
	 *    and the ACK flag is set. If so, break from loop; else, repeat.
	 */
	fprintf(stderr, "log: received SYN, sending SYNACK and listening for ACK\n");
	for (;;) {
		if (sendto(serverSocket, &serverSegment, HEADER_LEN, 0,
			(struct sockaddr *)&ackAddr, sizeof(ackAddr)) != HEADER_LEN) {
			perror("sendto");
			goto fail;
		}

		FD_ZERO(&readFds);
		FD_SET(serverSocket, &readFds);
		timeout = (struct timeval){ 0 };
		setMicroTime(&timeout, timeRemaining);
		gettimeofday(&startTime, NULL);
		fdsReady = select(serverSocket + 1, &readFds, NULL, NULL, &timeout);
		gettimeofday(&endTime, NULL);
		if (fdsReady < 0) {
			perror("select");
			goto fail;
		} else if (fdsReady == 0) {
			// Timed out
			fprintf(stderr, "warning: failed to receive ACK for SYNACK\n");
			timeRemaining = timeoutMicros = (int)(timeoutMicros * TIMEOUT_MULTIPLIER);
			continue;
		}

		// Nonblocking
		clientSegmentLen = recvfrom(serverSocket, &clientSegment,
			sizeof(struct TCPSegment), 0, NULL, NULL);
		if (clientSegmentLen < 0) {
			perror("recvfrom");
			goto fail;
		}

		convertTCPSegment(&clientSegment, 0);
		if (isChecksumValid(&clientSegment) && clientSegment.ackNum == ISN + 1
			&& isFlagSet(&clientSegment, ACK_FLAG)) {
			break;
		}

		timeElapsed = getMicroDiff(&startTime, &endTime);
		timeRemaining = MAX(timeRemaining - timeElapsed, 0);
	}

	nextExpectedClientSeq++;

	// Open file for writing
	int fd = open(fileStr, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
	if (fd < 0) {
		perror("open");
		exit(1);
	}
	ssize_t clientDataLen;  // amount of data excluding the TCP header
	uint32_t bytesReceived = 0;  // the number of bytes received, used for logging

	/*
	 * Receive file:
	 *  - The client sends the file, so all the server has to do is listen.
	 *  - When a segment is received, check if it is corrupted. If it is, then ignore it.
	 *  - Else, check if the FIN flag is set. If so, break from loop.
	 *  - Else, check the segment's seq. If the seq is the next expected one, write to the file
	 *    and update the next expected seq.
	 *  - Regardless if the seq is the next expected one, send an ACK to the client
	 *    specifying the next expected seq.
	 */
	fprintf(stderr, "log: receiving file\n");
	for (;;) {
		if ((clientSegmentLen = recvfrom(serverSocket, &clientSegment,
			sizeof(struct TCPSegment), 0, NULL, NULL)) < 0) {
			perror("recvfrom");
			close(fd);
			goto fail;
		}
		convertTCPSegment(&clientSegment, 0);
		if (isChecksumValid(&clientSegment)) {
			if (clientSegment.seqNum == nextExpectedClientSeq) {
				if (isFlagSet(&clientSegment, FIN_FLAG)) {
					break;
				}

				clientDataLen = clientSegmentLen - HEADER_LEN;
				fprintf(stderr, "log: received %d bytes\r", (bytesReceived += clientDataLen));
				if (write(fd, clientSegment.data, clientDataLen) != clientDataLen) {
					perror("write");
					close(fd);
					goto fail;
				}
				nextExpectedClientSeq += clientDataLen;
			}

			fillTCPSegment(&serverSegment, listenPort, ackPort, ISN + 1,
				nextExpectedClientSeq, ACK_FLAG, NULL, 0);
			convertTCPSegment(&serverSegment, 1);
			if (sendto(serverSocket, &serverSegment, HEADER_LEN, 0,
				(struct sockaddr *)&ackAddr, sizeof(ackAddr)) != HEADER_LEN) {
				perror("sendto");
				close(fd);
				goto fail;
			}
		}
	}

	fprintf(stderr, "\n");
	fsync(fd);
	close(fd);

	// Create and send ACK for client's FIN
	fillTCPSegment(&serverSegment, listenPort, ackPort, ISN + 1,
		nextExpectedClientSeq + 1, ACK_FLAG, NULL, 0);
	convertTCPSegment(&serverSegment, 1);
	fprintf(stderr, "log: received FIN, sending ACK\n");
	if (sendto(serverSocket, &serverSegment, HEADER_LEN, 0,
		(struct sockaddr *)&ackAddr, sizeof(ackAddr)) != HEADER_LEN) {
		perror("sento");
		goto fail;
	}

	// Create FIN segment
	struct TCPSegment finSegment;
	fillTCPSegment(&finSegment, listenPort, ackPort, ISN + 1,
		nextExpectedClientSeq + 1, FIN_FLAG, NULL, 0);
	convertTCPSegment(&finSegment, 1);

	timeRemaining = timeoutMicros;

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
		if (sendto(serverSocket, &finSegment, HEADER_LEN, 0,
			(struct sockaddr *)&ackAddr, sizeof(ackAddr)) != HEADER_LEN) {
			perror("sendto");
			goto fail;
		}

		FD_ZERO(&readFds);
		FD_SET(serverSocket, &readFds);
		timeout = (struct timeval){ 0 };
		setMicroTime(&timeout, timeRemaining);
		gettimeofday(&startTime, NULL);
		fdsReady = select(serverSocket + 1, &readFds, NULL, NULL, &timeout);
		gettimeofday(&endTime, NULL);
		if (fdsReady < 0) {
			perror("select");
			goto fail;
		} else if (fdsReady == 0) {
			fprintf(stderr, "warning: failed to receive ACK for FIN\n");
			timeRemaining = timeoutMicros = (int)(timeoutMicros * TIMEOUT_MULTIPLIER);
			continue;
		}

		// Nonblocking
		clientSegmentLen = recvfrom(serverSocket, &clientSegment,
			sizeof(struct TCPSegment), 0, NULL, NULL);
		if (clientSegmentLen < 0) {
			perror("recvfrom");
			goto fail;
		}

		convertTCPSegment(&clientSegment, 0);
		if (isChecksumValid(&clientSegment)) {
			if (clientSegment.ackNum == ISN + 2 && isFlagSet(&clientSegment, ACK_FLAG)) {
				break;
			}
			if (clientSegment.seqNum == nextExpectedClientSeq && isFlagSet(&clientSegment, FIN_FLAG)) {
				if (sendto(serverSocket, &serverSegment, HEADER_LEN, 0,
					(struct sockaddr *)&ackAddr, sizeof(ackAddr)) != HEADER_LEN) {
					perror("sendto");
					goto fail;
				}
			}
		}

		timeElapsed = getMicroDiff(&startTime, &endTime);
		timeRemaining = MAX(timeRemaining - timeElapsed, 0);
	}

	close(serverSocket);
	fprintf(stderr, "log: goodbye\n");
	return 0;

fail:
	close(serverSocket);
	return 1;
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

	return runServer(fileStr, listenPort, ackAddress, ackPort);
}
