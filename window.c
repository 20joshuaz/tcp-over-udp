#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "window.h"

struct Window *newWindow(int capacity) {
    struct TCPSegment *arr = (struct TCPSegment *)malloc(capacity * sizeof(struct TCPSegment));
    if(!arr) {
        return NULL;
    }
    struct Window *window = (struct Window *)malloc(sizeof(struct Window));
    if(!window) {
        free(arr);
        return NULL;
    }

    window->arr = arr;
    window->length = 0;
    window->capacity = capacity;
    window->startIndex = 0;
    window->endIndex = -1;
    return window;
}

void freeWindow(struct Window *window) {
    free(window->arr);
    free(window);
}

int isEmpty(struct Window *window) {
    return window->length == 0;
}

int isFull(struct Window *window) {
    return window->length == window->capacity;
}

int incrementIndexWithWraparound(int index, int maxIndex) {
    if(++index == maxIndex) {
        return 0;
    }
    return index;
}

void offer(struct Window *window, struct TCPSegment *segment) {
    assert(!isFull(window));
    window->endIndex = incrementIndexWithWraparound(window->endIndex, window->capacity);
    memcpy(window->arr + window->endIndex, segment, sizeof(struct TCPSegment));
    window->length++;
}

void deleteHead(struct Window *window) {
    assert(!isEmpty(window));
    window->startIndex = incrementIndexWithWraparound(window->startIndex, window->capacity);
    window->length--;
}

void getACKRange(struct Window *window, uint32_t *startACKPtr, uint32_t *endACKPtr) {
    assert(!isEmpty(window));
    struct TCPSegment *startSegment = window->arr + window->startIndex;
    struct TCPSegment *endSegment = window->arr + window->endIndex;
    *startACKPtr = startSegment->seqNum + startSegment->dataLen;
    *endACKPtr = endSegment->seqNum + endSegment->dataLen;
}

int isACKNumInRange(struct Window *window, uint32_t ackNum) {
    if(isEmpty(window)) {
        return 0;
    }
    uint32_t startACK, endACK;
    getACKRange(window, &startACK, &endACK);
    if(endACK > startACK) {
        return startACK <= ackNum && ackNum <= endACK;
    }
    return startACK <= ackNum || ackNum <= endACK;
}
