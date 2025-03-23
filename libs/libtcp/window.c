#include <stdlib.h>
#include <string.h>

#include "window.h"

/*
 * Construct a new TCP segment window
 */
struct Window *newWindow(int capacity)
{
	struct TCPSegmentEntry *arr = malloc(capacity * sizeof(struct TCPSegmentEntry));
	if (!arr) {
		return NULL;
	}
	struct Window *window = malloc(sizeof(struct Window));
	if (!window) {
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

/*
 * Free a window
 */
void freeWindow(struct Window *window)
{
	free(window->arr);
	free(window);
}

int isEmpty(const struct Window *window)
{
	return window->length == 0;
}

int isFull(const struct Window *window)
{
	return window->length == window->capacity;
}

/*
 * Get the next index in a window (since the index may wrap around)
 */
int next(const struct Window *window, int index)
{
	if (++index == window->capacity) {
		return 0;
	}
	return index;
}

/*
 * Offer a TCP segment entry to the window
 */
void offer(struct Window *window, const struct TCPSegmentEntry *entry)
{
	if (isFull(window)) {
		return;
	}
	memcpy(window->arr + window->endIndex, entry, sizeof(struct TCPSegmentEntry));
	window->endIndex = next(window, window->endIndex);
	window->length++;
}

/*
 * Delete the first segment in a window
 */
void deleteHead(struct Window *window)
{
	if (isEmpty(window)) {
		return;
	}
	window->startIndex = next(window, window->startIndex);
	window->length--;
}
