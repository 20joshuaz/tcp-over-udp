#define HEADER_LEN 20
#define MSS 576

#define ACK_FLAG 0x10
#define SYN_FLAG 0x02
#define FIN_FLAG 0x01

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

    int length;
};

void doNothing(int signum);
int getNthBit(int num, int n);
uint16_t calculateSumOfHeaderWords(struct TCPHeader header);
struct TCPSegment makeTCPSegment(uint16_t sourcePort, uint16_t destPort, uint32_t seqNum, uint32_t ackNum,
        uint8_t flags, char *data, int dataLen);
struct TCPSegment parseTCPSegment(const char *rawSegment);
int isFlagSet(struct TCPHeader header, uint8_t flag);
int doesChecksumAgree(struct TCPHeader header);
void printTCPHeader(struct TCPHeader header);
int isNumber(char *s);
int getPort(char *portStr);
int isValidIP(char *ip);
