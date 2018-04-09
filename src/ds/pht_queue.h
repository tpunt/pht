/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-present The PHP Group                             |
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
    void (*dtor)(void *);
} pht_queue_t;

void pht_queue_init(pht_queue_t *queue, void (*dtor)(void *));
void pht_queue_push(pht_queue_t *queue, void *element);
void *pht_queue_pop(pht_queue_t *queue);
void *pht_queue_front(pht_queue_t *queue);
int pht_queue_size(pht_queue_t *queue);
void pht_queue_destroy(pht_queue_t *queue);
void pht_queue_to_zend_hashtable(HashTable *zht, pht_queue_t *queue);

#endif
