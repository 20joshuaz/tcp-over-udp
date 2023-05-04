#ifndef VALIDATORS_H
#define VALIDATORS_H

#include <sys/time.h>

#define SI_MICRO ((int)1e6)

int isNumber(char *s);
int getPort(char *portStr);
int isValidIP(char *ip);
int getMicroTime(struct itimerval *it);
void setMicroTime(struct itimerval *it, int totalMicro);

#endif
