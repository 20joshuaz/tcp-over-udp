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

#include "window.h"
#include "tcp.h"
#include "helpers.h"

#define ISN 0
#define INITIAL_TIMEOUT 1  // The initial timeout, in seconds
#define TIMEOUT_MULTIPLIER 1.1  // The timeout multiplier when a timeout occurs
#define ALPHA 0.125
#define BETA 0.25
#define FINAL_WAIT 3  // How long the client waits after receiving an ACK for its FIN, in seconds

/*
 * Using the sample RTT, updates the estimated RTT, dev RTT, and timeout.
 */
void updateRTTAndTimeout(int sampleRTT, int *estimatedRTTPtr, int *devRTTPtr,
	int *timeoutPtr, float alpha, float beta)
{
	if (sampleRTT <= 0) {
		return;
	}
	if (*estimatedRTTPtr < 0) {
		// Estimated RTT has not been set yet (first sample RTT)
		*estimatedRTTPtr = sampleRTT;
		*devRTTPtr = sampleRTT / 2;
		*timeoutPtr = *estimatedRTTPtr + 4*(*devRTTPtr);
		return;
	}

	float newEstimatedRTT = (1 - alpha)*(*estimatedRTTPtr) + alpha*sampleRTT;
	float newDevRTT = (1 - beta)*(*devRTTPtr) + beta*abs(sampleRTT - *estimatedRTTPtr);
	float newTimeout = newEstimatedRTT + 4*newDevRTT;

	*estimatedRTTPtr = (int)newEstimatedRTT;
	*devRTTPtr = (int)newDevRTT;
	*timeoutPtr = (int)newTimeout;
}

int runClient(char *fileStr, char *udplAddress, int udplPort, int windowSize, int ackPort)
{
	// Create socket
	int clientSocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (clientSocket < 0) {
		perror("socket");
		exit(1);
	}

	// Bind socket to ackPort
	struct sockaddr_in clientAddr;
	memset(&clientAddr, 0, sizeof(clientAddr));
	clientAddr.sin_family = AF_INET;
	clientAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	clientAddr.sin_port = htons(ackPort);
	if (bind(clientSocket, (struct sockaddr *)&clientAddr, sizeof(clientAddr)) < 0) {
		perror("bind");
		goto fail;
	}

	struct sockaddr_in udplAddr;  // Address of newudpl
	memset(&udplAddr, 0, sizeof(udplAddr));
	udplAddr.sin_family = AF_INET;
	udplAddr.sin_addr.s_addr = inet_addr(udplAddress);
	udplAddr.sin_port = htons(udplPort);

	// clientSegment holds segments created by the client.
	// serverSegment holds segments received from the server.
	struct TCPSegment clientSegment, serverSegment;
	ssize_t serverSegmentLen;  // Amount of data in serverSegment
	int timeoutMicros = INITIAL_TIMEOUT * SI_MICRO;  // Transmission timeout
	int timeRemaining = timeoutMicros;
	int timeElapsed;
	int isSampleRTTBeingMeasured;  // Whether a segment's sample RTT is being measured
	struct timeval absoluteStartTime, startTime, endTime;
	int estimatedRTT = -1;
	int devRTT;

	// Create SYN segment
	fillTCPSegment(&clientSegment, ackPort, udplPort, ISN, 0, SYN_FLAG, NULL, 0);
	convertTCPSegment(&clientSegment, 1);
	isSampleRTTBeingMeasured = 1;  // SYN segment's sample RTT will be measured

	struct timeval timeout;
	fd_set readFds;
	int fdsReady;

	/*
	 * Send SYN:
	 *  - Send SYN.
	 *  - Call recvfrom. If nothing is received within the timeout, increase it
	 *    and mark the segment's sample RTT as not being measured.
	 *  - If a segment is received, check that is it not corrupted, the ACK is ISN + 1,
	 *    and the SYNACK flags are set. If so, update the RTT if the sample RTT was measured
	 *    and break from loop.
	 *  - Else, ignore and repeat.
	 */
	fprintf(stderr, "log: sending SYN\n");
	gettimeofday(&absoluteStartTime, NULL);
	for (;;) {
		if (sendto(clientSocket, &clientSegment, HEADER_LEN, 0,
			(struct sockaddr *)&udplAddr, sizeof(udplAddr)) != HEADER_LEN) {
			perror("sendto");
			goto fail;
		}

		FD_ZERO(&readFds);
		FD_SET(clientSocket, &readFds);
		timeout = (struct timeval){ 0 };
		setMicroTime(&timeout, timeRemaining);
		gettimeofday(&startTime, NULL);
		fdsReady = select(clientSocket + 1, &readFds, NULL, NULL, &timeout);
		gettimeofday(&endTime, NULL);
		if (fdsReady < 0) {
			perror("select");
			goto fail;
		} else if (fdsReady == 0) {
			// Timed out
			fprintf(stderr, "warning: failed to receive SYNACK\n");
			isSampleRTTBeingMeasured = 0;
			timeRemaining = timeoutMicros = (int)(timeoutMicros * TIMEOUT_MULTIPLIER);
			continue;
		}

		// Nonblocking
		serverSegmentLen = recvfrom(clientSocket, &serverSegment,
			sizeof(struct TCPSegment), 0, NULL, NULL);
		if (serverSegmentLen < 0) {
			perror("recvfrom");
			goto fail;
		}

		convertTCPSegment(&serverSegment, 0);
		if (isChecksumValid(&serverSegment) && serverSegment.ackNum == ISN + 1
			&& isFlagSet(&serverSegment, SYN_FLAG | ACK_FLAG)) {
			if (isSampleRTTBeingMeasured) {
				updateRTTAndTimeout(getMicroDiff(&absoluteStartTime, &endTime),
					&estimatedRTT, &devRTT, &timeoutMicros, ALPHA, BETA);
			}
			break;
		}

		timeElapsed = getMicroDiff(&startTime, &endTime);
		timeRemaining = MAX(timeRemaining - timeElapsed, 0);
	}

	uint32_t nextExpectedServerSeq = serverSegment.seqNum + 1;

	// Create and send ACK for server's SYNACK
	fillTCPSegment(&clientSegment, ackPort, udplPort, ISN + 1,
		nextExpectedServerSeq, ACK_FLAG, NULL, 0);
	convertTCPSegment(&clientSegment, 1);
	fprintf(stderr, "log: received SYNACK, sending ACK\n");
	if (sendto(clientSocket, &clientSegment, HEADER_LEN, 0,
		(struct sockaddr *)&udplAddr, sizeof(udplAddr)) != HEADER_LEN) {
		perror("sendto");
		goto fail;
	}

	uint32_t seqNum = ISN + 2;
	uint32_t seqNumBeingTimed;  // Seq of the segment whose sample RTT is being timed
	isSampleRTTBeingMeasured = 0;

	uint32_t bytesSent = 0;  // The number of bytes received, used for logging

	// Open file for reading
	int fd = open(fileStr, O_RDONLY);
	if (fd < 0) {
		perror("open");
		goto fail;
	}

	// fileSegment contains TCP segments with data from the file
	struct TCPSegmentEntry fileSegment;
	int fileSegmentLen;  // Amount of data in fileSegment
	char fileBuffer[MSS];
	size_t fileBufferLen;
	struct Window *window = newWindow(windowSize / MSS);  // Window of segments that are in transit
	if (!window) {
		perror("malloc");
		close(fd);
		goto fail;
	}

	timeRemaining = timeoutMicros;

	/*
	 * Send file:
	 *  - Fill window with segments and send all segments.
	 *    Choose a segment and start a timer to measure its RTT.
	 *  - Call recvfrom. If nothing is received within the timeout,
	 *    increase the timeout and resend all segments in window.
	 *    Mark the timed segment's RTT as invalid.
	 *  - If a segment is received, check if it is corrupted. If it is, then ignore it.
	 *  - Else, check the segment's ACK. If it is in the window, shift the window up to the ACK.
	 *    - If the segment being timed is ACKed, then stop its timer
	 *      and adjust the timeout based on the segment's RTT.
	 */
	fprintf(stderr, "log: sending file\n");
	do {
		while (!isFull(window) && (fileBufferLen = read(fd, fileBuffer, MSS)) > 0) {
			fillTCPSegment((struct TCPSegment *)&fileSegment, ackPort, udplPort, seqNum,
				nextExpectedServerSeq, 0, fileBuffer, fileBufferLen);
			// Store segments in network byte order
			convertTCPSegment((struct TCPSegment *)&fileSegment, 1);
			fileSegment.dataLen = fileBufferLen;
			fileSegmentLen = HEADER_LEN + fileBufferLen;
			offer(window, &fileSegment);
			if (!isSampleRTTBeingMeasured) {
				isSampleRTTBeingMeasured = 1;
				seqNumBeingTimed = seqNum;
				gettimeofday(&absoluteStartTime, NULL);
			}

			seqNum += fileSegment.dataLen;

			if (sendto(clientSocket, &fileSegment, fileSegmentLen, 0,
				(struct sockaddr *)&udplAddr, sizeof(udplAddr)) != fileSegmentLen) {
				perror("sendto");
				freeWindow(window);
				close(fd);
				goto fail;
			}
			fprintf(stderr, "log: sent %d bytes\r", (bytesSent += fileSegment.dataLen));
		}
		if (fileBufferLen < 0) {
			perror("read");
			freeWindow(window);
			close(fd);
			goto fail;
		}

		FD_ZERO(&readFds);
		FD_SET(clientSocket, &readFds);
		timeout = (struct timeval){ 0 };
		setMicroTime(&timeout, timeRemaining);
		gettimeofday(&startTime, NULL);
		fdsReady = select(clientSocket + 1, &readFds, NULL, NULL, &timeout);
		gettimeofday(&endTime, NULL);
		if (fdsReady < 0 ) {
			perror("select");
			freeWindow(window);
			close(fd);
			goto fail;
		} else if (fdsReady == 0) {
			timeRemaining = timeoutMicros = (int)(timeoutMicros * TIMEOUT_MULTIPLIER);

			int currIndex = window->startIndex;
			struct TCPSegmentEntry *segmentInWindow;
			int segmentInWindowLen;
			do {
				segmentInWindow = window->arr + currIndex;
				segmentInWindowLen = HEADER_LEN + segmentInWindow->dataLen;
				if (sendto(clientSocket, segmentInWindow, segmentInWindowLen, 0,
					(struct sockaddr *)&udplAddr, sizeof(udplAddr)) != segmentInWindowLen) {
					perror("sendto");
					freeWindow(window);
					close(fd);
					goto fail;
				}
			} while ((currIndex = next(window, currIndex)) != window->endIndex);
			isSampleRTTBeingMeasured = 0;
			continue;
		}

		// Nonblocking
		serverSegmentLen = recvfrom(clientSocket, &serverSegment,
			sizeof(struct TCPSegment), 0, NULL, NULL);
		if (serverSegmentLen < 0) {
			perror("recvfrom");
			freeWindow(window);
			close(fd);
			goto fail;
		}

		convertTCPSegment(&serverSegment, 0);
		int resumeTimer = 1;
		if (isChecksumValid(&serverSegment)) {
			const uint32_t serverACKNum = serverSegment.ackNum;
			if (serverACKNum > ntohl(window->arr[window->startIndex].segment.seqNum)
				&& isFlagSet(&serverSegment, ACK_FLAG)) {
				// isEmpty(window) || window->arr[window->startIndex].seqNum == serverACKNum
				for ( ; !isEmpty(window)
					&& ntohl(window->arr[window->startIndex].segment.seqNum) != serverACKNum;
					deleteHead(window));

				if (isSampleRTTBeingMeasured && !isEmpty(window)
					&& seqNumBeingTimed < ntohl(window->arr[window->startIndex].segment.seqNum)) {
					updateRTTAndTimeout(getMicroDiff(&absoluteStartTime, &endTime),
						&estimatedRTT, &devRTT, &timeoutMicros, ALPHA, BETA);
					isSampleRTTBeingMeasured = 0;
				}

				timeRemaining = timeoutMicros;
				resumeTimer = 0;
			}
			else if (serverACKNum == ISN + 1 && isFlagSet(&serverSegment, SYN_FLAG | ACK_FLAG)) {
				if (sendto(clientSocket, &clientSegment, HEADER_LEN, 0,
					(struct sockaddr *)&udplAddr, sizeof(udplAddr)) != HEADER_LEN) {
					perror("sendto");
					freeWindow(window);
					close(fd);
					goto fail;
				}
			}
			// else ACK out of range
		}
		if (resumeTimer) {
			timeElapsed = getMicroDiff(&startTime, &endTime);
			timeRemaining = MAX(timeRemaining - timeElapsed, 0);
		}
	} while (!isEmpty(window));

	fprintf(stderr, "\n");
	freeWindow(window);
	close(fd);

	// Create FIN segment
	fillTCPSegment(&clientSegment, ackPort, udplPort, seqNum++,
		nextExpectedServerSeq, FIN_FLAG, NULL, 0);
	convertTCPSegment(&clientSegment, 1);

	timeRemaining = timeoutMicros;

	/*
	 * Send FIN:
	 *  - Send FIN.
	 *  - Call recvfrom. If nothing is received within the timeout, increase it and repeat.
	 *  - If a segment is received, check that it is not corrupted, the ACK is for the next seq,
	 *    and the ACK flag is set. If so, break from loop; else, repeat.
	 */
	fprintf(stderr, "log: finished sending file, sending FIN\n");
	for (;;) {
		if (sendto(clientSocket, &clientSegment, HEADER_LEN, 0,
			(struct sockaddr *)&udplAddr, sizeof(udplAddr)) != HEADER_LEN) {
			perror("sendto");
			goto fail;
		}

		FD_ZERO(&readFds);
		FD_SET(clientSocket, &readFds);
		timeout = (struct timeval){ 0 };
		gettimeofday(&startTime, NULL);
		setMicroTime(&timeout, timeRemaining);
		gettimeofday(&endTime, NULL);
		fdsReady = select(clientSocket + 1, &readFds, NULL, NULL, &timeout);
		if (fdsReady < 0) {
			perror("select");
			goto fail;
		} else if (fdsReady == 0) {
			fprintf(stderr, "warning: failed to receive ACK for FIN\n");
			timeRemaining = timeoutMicros = (int)(timeoutMicros * TIMEOUT_MULTIPLIER);
			continue;
		}

		// Nonblocking
		serverSegmentLen = recvfrom(clientSocket, &serverSegment,
			sizeof(struct TCPSegment), 0, NULL, NULL);
		if (serverSegmentLen < 0) {
			perror("recvfrom");
			goto fail;
		}

		convertTCPSegment(&serverSegment, 0);
		if (isChecksumValid(&serverSegment) && serverSegment.ackNum == seqNum
			&& isFlagSet(&serverSegment, ACK_FLAG)) {
			break;
		}

		timeElapsed = getMicroDiff(&startTime, &endTime);
		timeRemaining = MAX(timeRemaining - timeElapsed, 0);
	}

	/*
	* Listen for FIN:
	*  - Call recvfrom. If the received segment is not corrupt, has a seq that is the next expected one,
	*    and has its FIN flag set, then break from loop.
	*/
	fprintf(stderr, "log: received ACK for FIN, listening for FIN\n");
	for (;;) {
		serverSegmentLen = recvfrom(clientSocket, &serverSegment,
			sizeof(struct TCPSegment), 0, NULL, NULL);
		if (serverSegmentLen < 0) {
			perror("recvfrom");
			goto fail;
		}

		convertTCPSegment(&serverSegment, 0);
		if (isChecksumValid(&serverSegment) && serverSegment.seqNum == nextExpectedServerSeq
			&& isFlagSet(&serverSegment, FIN_FLAG)) {
			break;
		}
	}

	// Create ACK for server's FIN
	fillTCPSegment(&clientSegment, ackPort, udplPort, seqNum,
		nextExpectedServerSeq + 1, ACK_FLAG, NULL, 0);
	convertTCPSegment(&clientSegment, 1);
	int hasSeenFIN = 1;  // Whether a FIN from the server has just been received
	timeRemaining = (int)(FINAL_WAIT * SI_MICRO);

	/*
	 * Send ACK:
	 *  - Send ACK.
	 *  - Call recvfrom. If nothing is received within the timeout,
	 *    break from loop and terminate program (final timeout).
	 *  - If a segment is received, check that it is not corrupt, the seq is the next expected one,
	 *    and the FIN flag is set. If so, resend the ACK. Else, ignore.
	 */
	fprintf(stderr, "log: received FIN, sending ACK and waiting %.1f seconds\n", (float)FINAL_WAIT);
	for (;;) {
		if (hasSeenFIN) {
			if (sendto(clientSocket, &clientSegment, HEADER_LEN, 0,
				(struct sockaddr *)&udplAddr, sizeof(udplAddr)) != HEADER_LEN) {
				perror("sendto");
				goto fail;
			}
		}

		FD_ZERO(&readFds);
		FD_SET(clientSocket, &readFds);
		timeout = (struct timeval){ 0 };
		setMicroTime(&timeout, timeRemaining);
		gettimeofday(&startTime, NULL);
		fdsReady = select(clientSocket + 1, &readFds, NULL, NULL, &timeout);
		gettimeofday(&endTime, NULL);
		if (fdsReady < 0) {
			perror("select");
			goto fail;
		} else if (fdsReady == 0) {
			break;
		}

		// Nonblocking
		serverSegmentLen = recvfrom(clientSocket, &serverSegment,
			sizeof(struct TCPSegment), 0, NULL, NULL);
		if (serverSegmentLen < 0) {
			perror("recvfrom");
			goto fail;
		}

		convertTCPSegment(&serverSegment, 0);
		if (isChecksumValid(&serverSegment) && serverSegment.seqNum == nextExpectedServerSeq
			&& isFlagSet(&serverSegment, FIN_FLAG)) {
			hasSeenFIN = 1;
		}

		timeElapsed = getMicroDiff(&startTime, &endTime);
		timeRemaining = MAX(timeRemaining - timeElapsed, 0);
	}

	close(clientSocket);
	fprintf(stderr, "log: goodbye\n");
	return 0;

fail:
	close(clientSocket);
	return 1;
}

int main(int argc, char **argv)
{
	if (argc != 6) {
		fprintf(stderr, "usage: tcpclient <file> <udpl address> <udpl port> <window size> <ack port>\n");
		exit(1);
	}

	char *fileStr = argv[1];
	if (access(fileStr, F_OK) != 0) {
		perror("access");
		exit(1);
	}
	char *udplAddress = argv[2];
	if (!isValidIP(udplAddress)) {
		fprintf(stderr, "error: invalid udpl address\n");
		exit(1);
	}
	int udplPort = getPort(argv[3]);
	if (!udplPort) {
		fprintf(stderr, "error: invalid udpl port\n");
		exit(1);
	}
	char *windowSizeStr = argv[4];
	if (!isNumber(windowSizeStr)) {
		fprintf(stderr, "error: invalid window size\n");
		exit(1);
	}
	int windowSize = (int)strtol(windowSizeStr, NULL, 10);
	if (windowSize < MSS) {
		fprintf(stderr, "error: window size must be at least %d\n", MSS);
		exit(1);
	}
	int ackPort = getPort(argv[5]);
	if (!ackPort) {
		fprintf(stderr, "error: invalid ack port\n");
		exit(1);
	}

	return runClient(fileStr, udplAddress, udplPort, windowSize, ackPort);
}
