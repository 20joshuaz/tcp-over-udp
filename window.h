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
int next(struct Window *window, int index);
void offer(struct Window *window, struct TCPSegment *segment);
void deleteHead(struct Window *window);

#endif
