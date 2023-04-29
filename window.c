#include <assert.h>
#include <stdlib.h>
#include <sys/socket.h>

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

void offer(struct Window *window, struct TCPSegment segment) {
    assert(!isFull(window));
    window->endIndex = incrementIndexWithWraparound(window->endIndex, window->capacity);
    window->arr[window->endIndex] = segment;
    window->length++;
}

void deleteHead(struct Window *window) {
    assert(!isEmpty(window));
    window->startIndex = incrementIndexWithWraparound(window->startIndex, window->capacity);
    window->length--;
}

void getSeqRange(struct Window *window, uint32_t *startSeqPtr, uint32_t *endSeqPtr) {
    struct TCPSegment startSegment, endSegment;
    startSegment = window->arr[window->startIndex];
    endSegment = window->arr[window->endIndex];
    *startSeqPtr = startSegment.header.seqNum + startSegment.dataLen;
    *endSeqPtr = endSegment.header.seqNum + endSegment.dataLen;
}

int isSeqNumInRange(struct Window *window, uint32_t seqNum) {
    assert(!isEmpty(window));
    uint32_t startSeq, endSeq;
    getSeqRange(window, &startSeq, &endSeq);
    if(endSeq > startSeq) {
        return startSeq <= seqNum && seqNum <= endSeq;
    }
    return startSeq <= seqNum || seqNum <= endSeq;
}

int moveWindowToSeqNum(struct Window *window, uint32_t seqNum) {
    if(!isSeqNumInRange(window, seqNum)) {
        return 0;
    }
    for( ; !isEmpty(window) && window->arr[window->startIndex].header.seqNum != seqNum; deleteHead(window));
    return 1;
    // isEmpty(window) || window->arr[window->startIndex].header.seqNum == seqNum
}
