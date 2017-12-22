#include <stdlib.h>

#include "pht_queue.h"

void queue_init(queue_t *queue)
{
    queue->elements = NULL;
    queue->last = NULL;
    queue->size = 0;
}

void enqueue(queue_t *queue, void *element)
{
    linked_list_t *ll = malloc(sizeof(linked_list_t));

    ll->element = element;
    ll->next = NULL;

    if (queue->elements) {
        queue->last->next = ll;
    } else {
        queue->elements = ll;
    }

    queue->last = ll;
    ++queue->size;
}

void *dequeue(queue_t *queue)
{
    linked_list_t *ll = NULL;
    void *element = NULL;

    if (queue->elements) {
        ll = queue->elements;
        queue->elements = queue->elements->next;

        if (!queue->elements) {
            queue->last = NULL;
        }

        element = ll->element;
        free(ll);
        --queue->size;
    }

    return element;
}
