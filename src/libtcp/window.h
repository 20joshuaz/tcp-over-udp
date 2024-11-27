#ifndef WINDOW_H
#define WINDOW_H

#include "tcp.h"

struct TCPSegmentEntry {
	struct TCPSegment segment;
	int dataLen;
};

struct Window {
	struct TCPSegmentEntry *arr;
	int length;
	int capacity;
	int startIndex;
	int endIndex;
};

struct Window *newWindow(int);
void freeWindow(struct Window *);
int isEmpty(const struct Window *);
int isFull(const struct Window *);
int next(const struct Window *, int);
void offer(struct Window *, struct TCPSegmentEntry *);
void deleteHead(struct Window *);

#endif
