#ifndef LIB_AGENT_MESSAGE_QUEUE_H
#define LIB_AGENT_MESSAGE_QUEUE_H

#include <stddef.h>

/**
 * message_queue - simple FIFO of strings.
 *
 * Used by the REPL to stage user messages typed while a turn is already
 * running in the async executor. Single-threaded: both push (from the
 * readline callback) and pop (from the select() loop) happen on the main
 * thread, so no internal locking is needed.
 */

typedef struct message_queue message_queue_t;

/**
 * Create an empty queue.
 *
 * @return New queue, or NULL on allocation failure.
 */
message_queue_t* message_queue_create(void);

/**
 * Destroy a queue and free any messages still queued.
 *
 * @param queue Queue to destroy, may be NULL.
 */
void message_queue_destroy(message_queue_t* queue);

/**
 * Push a copy of message onto the back of the queue.
 *
 * @param queue Queue to push onto.
 * @param message Null-terminated string to copy and enqueue.
 * @return 0 on success, -1 on invalid arguments or allocation failure.
 */
int message_queue_push(message_queue_t* queue, const char* message);

/**
 * Pop the message at the front of the queue.
 *
 * @param queue Queue to pop from.
 * @return Newly allocated string owned by the caller (must free()), or
 *         NULL if the queue is empty or invalid.
 */
char* message_queue_pop(message_queue_t* queue);

/**
 * Number of messages currently queued.
 *
 * @param queue Queue to inspect, may be NULL (returns 0).
 * @return Count of queued messages.
 */
size_t message_queue_count(const message_queue_t* queue);

#endif /* LIB_AGENT_MESSAGE_QUEUE_H */
