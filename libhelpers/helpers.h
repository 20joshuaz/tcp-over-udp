#ifndef VALIDATORS_H
#define VALIDATORS_H

#include <sys/time.h>

#define SI_MICRO ((int)1e6)

int isNumber(char *);
int getPort(char *);
int isValidIP(char *);
int getMicroTime(struct itimerval *);
void setMicroTime(struct itimerval *, int);

#endif
