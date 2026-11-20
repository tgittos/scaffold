#include "message_queue.h"
#include <stdlib.h>
#include <string.h>

typedef struct message_node {
    char* message;
    struct message_node* next;
} message_node_t;

struct message_queue {
    message_node_t* head;
    message_node_t* tail;
    size_t count;
};

message_queue_t* message_queue_create(void) {
    message_queue_t* queue = calloc(1, sizeof(message_queue_t));
    return queue;
}

void message_queue_destroy(message_queue_t* queue) {
    if (queue == NULL) {
        return;
    }
    message_node_t* node = queue->head;
    while (node != NULL) {
        message_node_t* next = node->next;
        free(node->message);
        free(node);
        node = next;
    }
    free(queue);
}

int message_queue_push(message_queue_t* queue, const char* message) {
    if (queue == NULL || message == NULL) {
        return -1;
    }

    message_node_t* node = calloc(1, sizeof(message_node_t));
    if (node == NULL) {
        return -1;
    }

    node->message = strdup(message);
    if (node->message == NULL) {
        free(node);
        return -1;
    }

    node->next = NULL;
    if (queue->tail == NULL) {
        queue->head = node;
        queue->tail = node;
    } else {
        queue->tail->next = node;
        queue->tail = node;
    }
    queue->count++;
    return 0;
}

char* message_queue_pop(message_queue_t* queue) {
    if (queue == NULL || queue->head == NULL) {
        return NULL;
    }

    message_node_t* node = queue->head;
    queue->head = node->next;
    if (queue->head == NULL) {
        queue->tail = NULL;
    }
    queue->count--;

    char* message = node->message;
    free(node);
    return message;
}

size_t message_queue_count(const message_queue_t* queue) {
    if (queue == NULL) {
        return 0;
    }
    return queue->count;
}
