#ifndef LINKEDLIST_H
#define LINKEDLIST_H

#include "tcp.h"

struct LinkedList {
    struct TCPSegment *arr;
    int length;
    int capacity;
    int startIndex;
    int endIndex;
};

struct LinkedList *newLinkedList(int capacity);
void freeLinkedList(struct LinkedList *ll);
int isEmpty(struct LinkedList *ll);
int isFull(struct LinkedList *ll);
void offer(struct LinkedList *ll, struct TCPSegment item);
struct TCPSegment poll(struct LinkedList *ll);

#endif
