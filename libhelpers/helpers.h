#ifndef HELPERS_H 
#define HELPERS_H

#include <sys/time.h>

#define SI_MICRO 1000000

#define MAX(a, b) ((a) > (b) ? (a) : (b))

int isNumber(char *);
int getPort(char *);
int isValidIP(char *);
int getMicroDiff(struct timeval *, struct timeval *);
void setMicroTime(struct timeval *, int);

#endif
