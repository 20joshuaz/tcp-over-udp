#include <assert.h>
#include <stdlib.h>

#include "linkedlist.h"

struct LinkedList *newLinkedList(int capacity) {
    struct TCPSegment *arr = (struct TCPSegment *)malloc(capacity * sizeof(struct TCPSegment));
    if(!arr) {
        return NULL;
    }
    struct LinkedList *ll = (struct LinkedList *)malloc(sizeof(struct LinkedList));
    if(!ll) {
        free(arr);
        return NULL;
    }

    ll->arr = arr;
    ll->length = 0;
    ll->capacity = capacity;
    ll->startIndex = 0;
    ll->endIndex = -1;
    return ll;
}

void freeLinkedList(struct LinkedList *ll) {
    free(ll->arr);
    free(ll);
}

int isEmpty(struct LinkedList *ll) {
    return ll->length == 0;
}

int isFull(struct LinkedList *ll) {
    return ll->length == ll->capacity;
}

int incrementIndexWithWraparound(int index, int maxIndex) {
    if(++index == maxIndex) {
        return 0;
    }
    return index;
}

void offer(struct LinkedList *ll, struct TCPSegment item) {
    assert(!isFull(ll));
    int newEndIndex = incrementIndexWithWraparound(ll->endIndex, ll->capacity);
    ll->arr[newEndIndex] = item;
    ll->endIndex = newEndIndex;
    ll->length++;
}

struct TCPSegment poll(struct LinkedList *ll) {
    assert(!isEmpty(ll));
    int currStartIndex = ll->startIndex;
    ll->startIndex = incrementIndexWithWraparound(currStartIndex, ll->capacity);
    ll->length--;
    return ll->arr[currStartIndex];
}
