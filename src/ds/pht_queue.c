/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2016 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Thomas Punt <tpunt@php.net>                                  |
  +----------------------------------------------------------------------+
*/

#include <stdlib.h>

#include "src/pht_entry.h"
#include "src/ds/pht_queue.h"

void pht_queue_init(pht_queue_t *queue, void (*dtor)(void *))
{
    queue->elements = NULL;
    queue->last = NULL;
    queue->size = 0;
    queue->dtor = dtor;
}

void pht_queue_push(pht_queue_t *queue, void *element)
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

void *pht_queue_pop(pht_queue_t *queue)
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

void *pht_queue_front(pht_queue_t *queue)
{
    if (!queue->elements) {
        return NULL;
    }

    return queue->elements->element;
}

int pht_queue_size(pht_queue_t *queue)
{
    return queue->size;
}

void pht_queue_destroy(pht_queue_t *queue)
{
    while (queue->size) {
        queue->dtor(pht_queue_pop(queue));
    }
}

void pht_queue_to_zend_hashtable(HashTable *zht, pht_queue_t *queue)
{
    linked_list_t *ll = queue->elements;

    for (int i = 0; ll; ++i, ll = ll->next) {
        zval value;

        pht_convert_entry_to_zval(&value, ll->element);
        _zend_hash_index_add(zht, i, &value ZEND_FILE_LINE_CC);
    }
}
