#ifndef PHT_QUEUE_H
#define PHT_QUEUE_H

typedef struct _linked_list_t {
    void *element;
    struct _linked_list_t *next;
} linked_list_t;

typedef struct _queue_t {
    linked_list_t *elements;
    linked_list_t *last;
    int size;
} queue_t;

void queue_init(queue_t *queue);
void enqueue(queue_t *queue, void *element);
void *dequeue(queue_t *queue);
int queue_size(queue_t *queue);
void pht_queue_to_zend_hashtable(HashTable *zht, queue_t *queue);

#endif
