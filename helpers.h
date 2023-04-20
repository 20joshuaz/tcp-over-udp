#define HEADER_LEN 20
#define MSS 576

#define DO_NOTHING_ON_ALARM \
    struct sigaction sa; \
    memset(&sa, 0, sizeof(sa)); \
    sa.sa_handler = &doNothing; \
    sigaction(SIGALRM, &sa, NULL);

#define WRAP_IN_ALARM(stmt, timeout) \
    alarm(timeout); \
    stmt; \
    alarm(0);

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

void doNothing(int signum);
int getNthBit(int num, int n);
uint16_t calculateSumOfHeaderWords(struct TCPSegment segment);
struct TCPSegment createTCPSegment(uint16_t sourcePort, uint16_t destPort, uint32_t seqNum, uint32_t ackNum,
                                 int setACK, int setSYN, int setFIN, char *data, int dataLen);
struct TCPSegment parseToTCPSegment(char *rawSegment, int segmentLen);
int isACKSet(uint8_t flags);
int isSYNSet(uint8_t flags);
int isFINSet(uint8_t flags);
int isNumber(char *s);
int getPort(char *portStr);
int isValidIP(char *ip);
