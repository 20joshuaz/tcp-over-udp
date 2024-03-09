#ifndef WINDOW_H
#define WINDOW_H

#include "tcp.h"

struct Window {
	struct TCPSegment *arr;
	int length;
	int capacity;
	int startIndex;
	int endIndex;
};

struct Window *newWindow(int);
void freeWindow(struct Window *);
int isEmpty(struct Window *);
int isFull(struct Window *);
int next(struct Window *, int);
void offer(struct Window *, struct TCPSegment *);
void deleteHead(struct Window *);

#endif
