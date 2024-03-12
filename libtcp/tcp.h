#ifndef TCP_H
#define TCP_H

#include <assert.h>
#include <stdint.h>

#define HEADER_LEN 20
#define MSS 576

#define ACK_FLAG 0x10
#define SYN_FLAG 0x02
#define FIN_FLAG 0x01

struct TCPSegment {
	uint16_t sourcePort;
	uint16_t destPort;
	uint32_t seqNum;
	uint32_t ackNum;
	uint8_t length;
	uint8_t flags;
	uint16_t recvWindow;
	uint16_t checksum;
	uint16_t urgentPtr;

	char data[MSS];
} __attribute__((packed));

static_assert(sizeof(struct TCPSegment) == HEADER_LEN + MSS,
	"TCPSegment struct not packed");

uint16_t calculateSumOfHeaderWords(struct TCPSegment *);
int isFlagSet(struct TCPSegment *, uint8_t);
void fillTCPSegment(struct TCPSegment *, uint16_t, uint16_t,
	uint32_t, uint32_t, uint8_t, char *, int);
void convertTCPSegment(struct TCPSegment *, int);
int isChecksumValid(struct TCPSegment *);
void printTCPHeader(struct TCPSegment *);

#endif
