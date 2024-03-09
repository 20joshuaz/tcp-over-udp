#include <stdio.h>
#include <string.h>

#include "tcp.h"

/*
 * Checks if the nth bit is set in num.
 */
int getNthBit(int num, int n)
{
	return num >> n & 1;
}

/*
 * Sets the nth bit in num to 1.
 */
int setNthBit(int num, int n)
{
	return num | (1 << n);
}

/*
 * Performs a bitwise sum between a and b. Stores the result and carryover.
 */
void doBitwiseSum(uint16_t a, uint16_t b, uint16_t *resPtr, uint16_t *carryoverPtr)
{
	uint16_t res = 0;
	int carryover = 0;
	for (int i = 0; i < 16; i++) {
		int currA = getNthBit(a, i);
		int currB = getNthBit(b, i);

		int sum = currA + currB + carryover;
		if (sum == 1) {
			res = setNthBit(res, i);
			carryover = 0;
		} else if (sum == 2) {
			carryover = 1;
		} else if (sum == 3) {
			res = setNthBit(res, i);
			carryover = 1;
		}
	}
	*resPtr = res;
	*carryoverPtr = carryover;
}

/*
 * Calculates the sum between a and b. Wraps carryovers around.
 */
uint16_t calculateSumWithOverflow(uint16_t a, uint16_t b)
{
	do {
		doBitwiseSum(a, b, &a, &b);
	} while (b);
	return a;
}

/*
 * Calculates the sum of all the words in the header.
 */
uint16_t calculateSumOfHeaderWords(struct TCPSegment *segment)
{
	uint16_t *rawSegmentPtr = (uint16_t *)segment;
	uint16_t sum = 0;
	for (int i = 0; i < HEADER_LEN / 2; i++) {
		sum = calculateSumWithOverflow(sum, *(rawSegmentPtr + i));
	}
	return sum;
}

/*
 * Checks whether a flag is set in a header.
 */
int isFlagSet(struct TCPSegment *segment, uint8_t flag)
{
	return (segment->flags & flag) == flag;
}

/*
 * Fills a TCP segment.
 */
void fillTCPSegment(struct TCPSegment *segment, uint16_t sourcePort, uint16_t destPort,
        uint32_t seqNum, uint32_t ackNum, uint8_t flags, char *data, int dataLen)
{
	segment->sourcePort = sourcePort;
	segment->destPort = destPort;
	segment->seqNum = seqNum;
	segment->ackNum = ackNum;
	segment->length = 0x50;  // 01010000
	segment->flags = flags;
	segment->recvWindow = 0;
	segment->checksum = 0;
	segment->urgentPtr = 0;

	uint16_t checksum = calculateSumOfHeaderWords(segment);
	segment->checksum = ~checksum;

	memcpy(segment->data, data, dataLen);
	segment->dataLen = dataLen;
}

/*
 * Determines whether a segment is corrupt using its checksum.
 */
int isChecksumValid(struct TCPSegment *segment)
{
	uint16_t totalSum = calculateSumOfHeaderWords(segment);
	for (int i = 0; i < 16; i++) {
		if (!getNthBit(totalSum, i)) {
			return 0;
		}
	}
	return 1;
}

/*
 * Print a TCP header.
 */
void printTCPHeader(struct TCPSegment *segment)
{
	fprintf(stderr, "{\n");
	fprintf(stderr, "\tsourcePort: %d\n", segment->sourcePort);
	fprintf(stderr, "\tdestPort: %d\n", segment->destPort);
	fprintf(stderr, "\tseqNum: %d\n", segment->seqNum);
	fprintf(stderr, "\tackNum: %d\n", segment->ackNum);
	fprintf(stderr, "\tlength: %d\n", segment->length);
	fprintf(stderr, "\tflags: %x\n", segment->flags);
	fprintf(stderr, "\trecvWindow: %d\n", segment->recvWindow);
	fprintf(stderr, "\tchecksum: %x\n", segment->checksum);
	fprintf(stderr, "\turgentPtr: %d\n", segment->urgentPtr);
	fprintf(stderr, "}\n");
}
