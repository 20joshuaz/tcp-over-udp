#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "helpers.h"

/*
 * Check whether a string is a number (contains only numerals)
 */
int isNumber(const char *s)
{
	for (const char *trav = s; *trav; trav++) {
		if (!isdigit(*trav)) {
			return 0;
		}
	}
	return *s != 0;  // returns false for empty string
}

/*
 * Extract a port from a string
 */
int getPort(const char *portStr)
{
	if (!isNumber(portStr)) {
		return 0;
	}
	int port = strtol(portStr, NULL, 10);
	return (port >= 1024 && port <= 65535) * port;
}

/*
 * Check whether a string is a valid IP address
 */
int isValidIP(const char *ip)
{
	const char *trav = ip;
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
	char *stringp = ipCopy;
	const char *token;

	while (stringp) {
		token = strsep(&stringp, ".");
		if (!isNumber(token)) {
			return 0;
		}
		int part = strtol(token, NULL, 10);
		if (!(part >= 0 && part <= 255)) {
			return 0;
		}
	}
	return 1;
}

/*
 * Get the number of microseconds between two timevals
 */
int getMicroDiff(const struct timeval *startTime, const struct timeval *endTime)
{
	return (endTime->tv_sec - startTime->tv_sec)*SI_MICRO
		+ (endTime->tv_usec - startTime->tv_usec);
}

/*
 * Set a timeval's fields to the specified number of microseconds
 */
void setMicroTime(struct timeval *tv, int totalMicro)
{
	tv->tv_sec = totalMicro / SI_MICRO;
	tv->tv_usec = totalMicro % SI_MICRO;
}
