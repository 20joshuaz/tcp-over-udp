#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "helpers.h"

/*
 * Checks whether a string is a number (contains only numerals).
 */
int isNumber(char *s)
{
	for (char *trav = s; *trav; trav++) {
		if (!isdigit(*trav)) {
			return 0;
		}
	}
	return *s != 0;  // returns false for empty string
}

/*
 * Extracts a port from a string.
 */
int getPort(char *portStr)
{
	if (!isNumber(portStr)) {
		return 0;
	}
	int port = strtol(portStr, NULL, 10);
	return (port >= 1024 && port <= 65535) * port;
}

/*
 * Checks whether a string is a valid IP address.
 */
int isValidIP(char *ip)
{
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
	char *stringp = ipCopy;
	char *token;

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
 * Gets the number of microseconds from an itimerval's it_value field.
 */
int getMicroTime(struct itimerval *it)
{
	return it->it_value.tv_sec*SI_MICRO + it->it_value.tv_usec;
}

/*
 * Sets an itimerval's it_value field to the specified number of microseconds.
 */
void setMicroTime(struct itimerval *it, int totalMicro)
{
	it->it_value.tv_sec = totalMicro / SI_MICRO;
	it->it_value.tv_usec = totalMicro % SI_MICRO;
}
