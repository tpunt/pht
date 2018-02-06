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
#include <string.h>

#include "src/pht_entry.h"
#include "src/ds/pht_hashtable.h"

static void *pht_hashtable_search_direct(pht_hashtable_t *ht, pht_string_t *key, long hash);
static pht_string_t *pht_hashtable_key_fetch_direct(pht_hashtable_t *ht, pht_string_t *key, long hash);
static void pht_hashtable_insert_direct(pht_hashtable_t *ht, pht_string_t *key, long hash, void *value);
void pht_hashtable_update_direct(pht_hashtable_t *ht, pht_string_t *key, long hash, void *value);
void pht_hashtable_delete_direct(pht_hashtable_t *ht, pht_string_t *key, long hash);
static void pht_hashtable_resize(pht_hashtable_t *ht);
static void pht_hashtable_repopulate(pht_hashtable_t *ht, pht_bucket_t *old_values, int old_size);
static long get_hash(pht_string_t *key);

void pht_hashtable_init(pht_hashtable_t *ht, int size, void (*dtor)(void *))
{
    ht->values = calloc(sizeof(pht_bucket_t), size);
    ht->size = size;
    ht->used = 0;
    ht->dtor = dtor;
}

void pht_hashtable_destroy(pht_hashtable_t *ht)
{
    for (int i = 0; i < ht->size; ++i) {
        pht_bucket_t *b = ht->values + i;

        if (!b->value) {
            continue;
        }

        ht->dtor(b->value);

        if (b->key) {
            free(PHT_STRV_P(b->key));
            free(b->key);
        }
    }

    free(ht->values);
}

void pht_hashtable_insert_ind(pht_hashtable_t *ht, long hash, void *value)
{
    // resize at 75% capacity
    if (ht->used == ht->size - (ht->size >> 2)) {
        pht_hashtable_resize(ht);
    }

    pht_hashtable_insert_direct(ht, NULL, hash, value);
}

void pht_hashtable_insert(pht_hashtable_t *ht, pht_string_t *key, void *value)
{
    // resize at 75% capacity
    if (ht->used == ht->size - (ht->size >> 2)) {
        pht_hashtable_resize(ht);
    }

    pht_hashtable_insert_direct(ht, key, get_hash(key), value);
}

static void pht_hashtable_insert_direct(pht_hashtable_t *ht, pht_string_t *key, long hash, void *value)
{
    int index = hash & (ht->size - 1);
    int variance = 0;

    for (int i = 0; i < ht->size; ++i) {
        pht_bucket_t *b = ht->values + index;

        if (!b->value) {
            b->hash = hash;
            b->key = key;
            b->value = value;
            b->variance = variance;

            break;
        }

        if (variance > b->variance) {
            long tmp_hash = b->hash;
            void *tmp_value = b->value;
            int tmp_variance = b->variance;
            pht_string_t *tmp_key = b->key;

            b->hash = hash;
            b->key = key;
            b->value = value;
            b->variance = variance;

            hash = tmp_hash;
            key = tmp_key;
            value = tmp_value;
            variance = tmp_variance;
        }

        ++variance;

        if (++index == ht->size) {
            index = 0;
        }
    }

    ++ht->used;
}

static void pht_hashtable_resize(pht_hashtable_t *ht)
{
    pht_bucket_t *old_values = ht->values;
    int old_size = ht->size;

    ht->size <<= 1;
    ht->used = 0;
    ht->values = calloc(sizeof(pht_bucket_t), ht->size);

    pht_hashtable_repopulate(ht, old_values, old_size);

    free(old_values);
}

static void pht_hashtable_repopulate(pht_hashtable_t *ht, pht_bucket_t *old_values, int old_size)
{
    for (int i = 0; i < old_size; ++i) {
        if (old_values[i].value) {
            pht_hashtable_insert_direct(ht, old_values[i].key, old_values[i].hash, old_values[i].value);
        }
    }
}

// @todo make decent
static long get_hash(pht_string_t *key)
{
    return zend_hash_func(PHT_STRV_P(key), PHT_STRL_P(key));
}

void *pht_hashtable_search_ind(pht_hashtable_t *ht, long hash)
{
    return pht_hashtable_search_direct(ht, NULL, hash);
}

void *pht_hashtable_search(pht_hashtable_t *ht, pht_string_t *key)
{
    return pht_hashtable_search_direct(ht, key, get_hash(key));
}

void *pht_hashtable_search_direct(pht_hashtable_t *ht, pht_string_t *key, long hash)
{
    int index = hash & (ht->size - 1);

    for (int i = 0; i < ht->size; ++i) {
        pht_bucket_t *b = ht->values + index;

        if (b->value) {
            if (b->hash == hash && !(!!b->key ^ !!key) && (!key || pht_str_eq(b->key, key))) {
                return b->value;
            }
        } else if (!b->hash) { // b->hash = 0 for an empty space, 1 for a tombstone
            return NULL;
        } // @todo if backtracking was implemented then this could be returned from instead

        // @todo if the variance is less than the previous bucket, then also break early?

        if (++index == ht->size) {
            index = 0;
        }
    }

    return NULL;
}

pht_string_t *pht_hashtable_key_fetch(pht_hashtable_t *ht, pht_string_t *key)
{
    return pht_hashtable_key_fetch_direct(ht, key, get_hash(key));
}

static pht_string_t *pht_hashtable_key_fetch_direct(pht_hashtable_t *ht, pht_string_t *key, long hash)
{
    int index = hash & (ht->size - 1);

    for (int i = 0; i < ht->size; ++i) {
        pht_bucket_t *b = ht->values + index;

        if (b->value) {
            if (b->hash == hash && !(!!b->key ^ !!key) && (!key || pht_str_eq(b->key, key))) {
                return b->key;
            }
        } else if (!b->hash) { // b->hash = 0 for an empty space, 1 for a tombstone
            return NULL;
        } // @todo if backtracking was implemented then this could be returned from instead

        // @todo if the variance is less than the previous bucket, then also break early?

        if (++index == ht->size) {
            index = 0;
        }
    }

    return NULL;
}

void pht_hashtable_update_ind(pht_hashtable_t *ht, long hash, void *value)
{
    pht_hashtable_update_direct(ht, NULL, hash, value);
}

void pht_hashtable_update(pht_hashtable_t *ht, pht_string_t *key, void *value)
{
    pht_hashtable_update_direct(ht, key, get_hash(key), value);
}

void pht_hashtable_update_direct(pht_hashtable_t *ht, pht_string_t *key, long hash, void *value)
{
    int index = hash & (ht->size - 1);

    for (int i = 0; i < ht->size; ++i) {
        pht_bucket_t *b = ht->values + index;

        if (b->hash == hash && !(!!b->key ^ !!key) && (!key || pht_str_eq(b->key, key))) {
            ht->dtor(b->value);
            b->value = value;
            break;
        }

        if (++index == ht->size) {
            index = 0;
        }
    }
}

void pht_hashtable_delete_ind(pht_hashtable_t *ht, long hash)
{
    pht_hashtable_delete_direct(ht, NULL, hash);
}

void pht_hashtable_delete(pht_hashtable_t *ht, pht_string_t *key)
{
    pht_hashtable_delete_direct(ht, key, get_hash(key));
}

void pht_hashtable_delete_direct(pht_hashtable_t *ht, pht_string_t *key, long hash)
{
    int index = hash & (ht->size - 1);

    for (int i = 0; i < ht->size; ++i) {
        pht_bucket_t *b = ht->values + index;

        if (b->value) {
            if (b->hash == hash && !(!!b->key ^ !!key) && (!key || pht_str_eq(b->key, key))) {
                ht->dtor(b->value);

                if (b->key) {
                    free(PHT_STRV_P(b->key));
                    free(b->key);
                }

                b->key = NULL;
                b->hash = 1; // tombstone
                b->value = NULL;
                b->variance = 0;
                --ht->used;

                // @todo implement backtracking?

                break;
            }
        } else if (!b->hash) { // b->hash = 0 for an empty space, 1 for a tombstone
            return;
        } // @todo if backtracking was implemented then this could be returned from instead

        // @todo if the variance is less than the previous bucket, then also break early?

        if (++index == ht->size) {
            index = 0;
        }
    }
}

void pht_hashtable_to_zend_hashtable(HashTable *zht, pht_hashtable_t *pht)
{
    for (int i = 0; i < pht->size; ++i) {
        pht_bucket_t *b = pht->values + i;
        zval value;

        if (!b->value) {
            continue;
        }

        pht_convert_entry_to_zval(&value, b->value);

        if (b->key) {
            _zend_hash_str_add(zht, PHT_STRV_P(b->key), PHT_STRL_P(b->key), &value ZEND_FILE_LINE_CC);
        } else {
            _zend_hash_index_add(zht, b->hash, &value ZEND_FILE_LINE_CC);
        }
    }
}
