#define HEADER_LEN 20
#define MSS 576

#define DO_NOTHING_ON_ALARM \
    struct sigaction sa; \
    memset(&sa, 0, sizeof(sa)); \
    sa.sa_handler = &doNothing; \
    sigaction(SIGALRM, &sa, NULL);

struct TCPHeader {
    uint16_t sourcePort;
    uint16_t destPort;
    uint32_t seqNum;
    uint32_t ackNum;
    uint8_t length;
    uint8_t flags;
    uint16_t recvWindow;
    uint16_t checksum;
    uint16_t urgentPtr;
};

struct TCPSegment {
    struct TCPHeader header;
    char data[MSS];
};

void doNothing(int signum);
int getNthBit(int num, int n);
uint16_t calculateSumOfHeaderWords(struct TCPHeader header);
struct TCPSegment createTCPSegment(uint16_t sourcePort, uint16_t destPort, uint32_t seqNum, uint32_t ackNum,
                                 int setACK, int setSYN, int setFIN, char *data, int dataLen);
struct TCPSegment parseTCPSegment(char *rawSegment, int segmentLen);
int isACKSet(struct TCPHeader header);
int isSYNSet(struct TCPHeader header);
int isFINSet(struct TCPHeader header);
int doesChecksumAgree(struct TCPHeader header);
int isNumber(char *s);
int getPort(char *portStr);
int isValidIP(char *ip);
