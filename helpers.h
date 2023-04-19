#define HEADER_LEN 20
#define MSS 576

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
};

int getNthBit(int num, int n);
uint16_t calculateSumOfHeaderWords(struct TCPSegment segment);
struct TCPSegment fillTCPSegment(uint16_t sourcePort, uint16_t destPort, uint32_t seqNum, uint32_t ackNum,
                                 int setACK, int setSYN, int setFIN, char *data, int dataLen);
struct TCPSegment parseTCPSegment(char *rawSegment, int segmentLen);
int getPort(char *portStr);
int isValidIP(char *ip);
