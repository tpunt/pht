#ifndef PHT_QUEUE_H
#define PHT_QUEUE_H

typedef struct _linked_list_t {
    void *element;
    struct _linked_list_t *next;
} linked_list_t;

typedef struct _pht_queue_t {
    linked_list_t *elements;
    linked_list_t *last;
    int size;
} pht_queue_t;

void queue_init(pht_queue_t *queue);
void enqueue(pht_queue_t *queue, void *element);
void *dequeue(pht_queue_t *queue);
int queue_size(pht_queue_t *queue);
void pht_queue_to_zend_hashtable(HashTable *zht, pht_queue_t *queue);

#endif
