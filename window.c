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
    window->endIndex = 0;
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

int next(struct Window *window, int index) {
    if(++index == window->capacity) {
        return 0;
    }
    return index;
}

void offer(struct Window *window, struct TCPSegment *segment) {
    if(isFull(window)) {
        return;
    }
    memcpy(window->arr + window->endIndex, segment, sizeof(struct TCPSegment));
    window->endIndex = next(window, window->endIndex);
    window->length++;
}

void deleteHead(struct Window *window) {
    if(isEmpty(window)) {
        return;
    }
    window->startIndex = next(window, window->startIndex);
    window->length--;
}
