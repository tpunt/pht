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

#include <stdlib.h>

#include "src/pht_entry.h"
#include "src/ds/pht_vector.h"

static void pht_vector_space_check(pht_vector_t *vector)
{
    if (vector->used == vector->size) {
        vector->size = vector->size ? vector->size << 1 : 1;
        vector->values = realloc(vector->values, vector->size * sizeof(pht_entry_t *)); // @todo success check
    }
}

void pht_vector_init(pht_vector_t *vector, int size, void (*dtor)(void *))
{
    vector->values = calloc(size, sizeof(pht_entry_t *));
    vector->size = size;
    vector->used = 0;
    vector->dtor = dtor;
}

void pht_vector_push(pht_vector_t *vector, pht_entry_t *value)
{
    pht_vector_space_check(vector);

    vector->values[vector->used++] = value;
}

pht_entry_t *pht_vector_pop(pht_vector_t *vector)
{
    if (!vector->used) {
        return NULL;
    }
    // @todo resize if used = 1/4 of size?

    return vector->values[--vector->used];
}

pht_entry_t *pht_vector_shift(pht_vector_t *vector)
{
    if (!vector->used) {
        return NULL;
    }

    pht_entry_t *value = vector->values[0];

    for (int i = 1; i < vector->used; ++i) {
        vector->values[i - 1] = vector->values[i];
    }

    --vector->used;
    // @todo resize if used = 1/4 of size?

    return value;
}

void pht_vector_unshift(pht_vector_t *vector, pht_entry_t *value)
{
    pht_vector_space_check(vector);

    for (int i = vector->used; i; --i) {
        vector->values[i] = vector->values[i - 1];
    }

    vector->values[0] = value;
    ++vector->used;
}

pht_entry_t *pht_vector_fetch_at(pht_vector_t *vector, zend_long i)
{
    if (i < 0 || i >= vector->used) {
        return NULL;
    }

    return vector->values[i];
}

int pht_vector_update_at(pht_vector_t *vector, pht_entry_t *value, zend_long i)
{
    if (i < 0 || i >= vector->used) {
        return 0;
    }

    pht_entry_delete(vector->values[i]);
    vector->values[i] = value;

    return 1;
}

int pht_vector_insert_at(pht_vector_t *vector, pht_entry_t *value, zend_long i)
{
    if (i < 0 || i > vector->used) { // can be used like push()
        return 0;
    }

    pht_vector_space_check(vector);

    for (int i2 = vector->used; i2 > i; --i2) {
        vector->values[i2] = vector->values[i2 - 1];
    }

    vector->values[i] = value;
    ++vector->used;

    return 1;
}

int pht_vector_delete_at(pht_vector_t *vector, zend_long i)
{
    if (i < 0 || i >= vector->used) {
        return 0;
    }

    pht_entry_delete(vector->values[i]);

    while (i < vector->used) {
        vector->values[i] = vector->values[i + 1];
        ++i;
    }

    --vector->used;

    // @todo resize if used = 1/4 of size?

    return 1;
}

int pht_vector_size(pht_vector_t *vector)
{
    return vector->used;
}

void pht_vector_destroy(pht_vector_t *vector)
{
    for (int i = 0; i < vector->used; ++i) {
        vector->dtor(vector->values[i]);
    }

    free(vector->values);
}

void pht_vector_to_zend_hashtable(HashTable *zht, pht_vector_t *vector)
{
    for (int i = 0; i < vector->used; ++i) {
        zval value;

        pht_convert_entry_to_zval(&value, vector->values[i]);
        _zend_hash_index_add(zht, i, &value ZEND_FILE_LINE_CC);
    }
}
