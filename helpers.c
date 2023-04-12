#include <ctype.h>
#include <stdlib.h>
#include <string.h>

int isNumber(char *s) {
    for (char *trav = s; *trav; trav++) {
        if (!isdigit(*trav)) {
            return 0;
        }
    }
    return *s;
}

int getPort(char *portStr) {
    if (!isNumber(portStr)) {
        return 0;
    }
    int port = atoi(portStr);
    return (port >= 1024 && port <= 65535) * port;
}

int isValidIP(char *ip) {
    char *trav = ip;
    int numDots = 0;
    char curr;
    while ((curr = *trav++)) {
        numDots += (curr == '.');
    }
    if (numDots != 3) {
        return 0;
    }

    char ipCopy[strlen(ip) + 1];
    strcpy(ipCopy, ip);

    char delim[] = ".";
    char *token = strtok(ipCopy, delim);
    while (token) {
        if (!isNumber(token)) {
            return 0;
        }
        int part = atoi(token);
        if (!(part >= 0 && part <= 255)) {
            return 0;
        }
        token = strtok(NULL, delim);
    }
    return 1;
}
