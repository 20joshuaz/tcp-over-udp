#ifndef HELPERS_H 
#define HELPERS_H

#include <sys/time.h>

#define SI_MICRO 1000000

#define MAX(a, b) ((a) > (b) ? (a) : (b))

int isNumber(const char *);
int getPort(const char *);
int isValidIP(const char *);
int getMicroDiff(const struct timeval *, const struct timeval *);
void setMicroTime(struct timeval *, int);

#endif
