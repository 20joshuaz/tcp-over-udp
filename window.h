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

struct Window *newWindow(int capacity);
void freeWindow(struct Window *window);
int isEmpty(struct Window *window);
int isFull(struct Window *window);
void offer(struct Window *window, struct TCPSegment segment);
int moveWindowToSeqNum(struct Window *window, uint32_t seqNum);

#endif
