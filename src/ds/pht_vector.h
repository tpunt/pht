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

#ifndef PHT_VECTOR_H
#define PHT_VECTOR_H

#include <Zend/zend_long.h>
#include <Zend/zend_types.h>

typedef struct _entry_t entry_t;

typedef struct _pht_vector_t {
    entry_t **values;
    int size;
    int used;
} pht_vector_t;

void pht_vector_init(pht_vector_t *vector, int size);
void pht_vector_push(pht_vector_t *vector, entry_t *value);
entry_t *pht_vector_pop(pht_vector_t *vector);
entry_t *pht_vector_shift(pht_vector_t *vector);
void pht_vector_unshift(pht_vector_t *vector, entry_t *value);
int pht_vector_delete(pht_vector_t *vector, zend_long i);
int pht_vector_size(pht_vector_t *vector);
void pht_vector_to_zend_hashtable(HashTable *zht, pht_vector_t *vector);

#endif
