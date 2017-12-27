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
  | Author:                                                              |
  +----------------------------------------------------------------------+
*/

#ifndef PHT_HASHTABLE_H
#define PHT_HASHTABLE_H

// hash table flags
#define FREE_KEYS 1

// @todo store hash value in pht_string_t instead?
typedef struct _pht_bucket_t {
    pht_string_t *key; // @todo remove pointer to key?
    void *value;
    int hash;
    int variance;
} pht_bucket_t;

typedef struct _pht_hashtable_t {
    pht_bucket_t *values;
    int size;
    int used;
    int flags;
} pht_hashtable_t;

void pht_hashtable_init(pht_hashtable_t *ht, int size);
void pht_hashtable_insert(pht_hashtable_t *ht, pht_string_t *key, void *value);
void pht_hashtable_insert_ind(pht_hashtable_t *ht, int hash, void *value);
void pht_hashtable_delete(pht_hashtable_t *ht, pht_string_t *key, void (*dtor_value)(void *));
void pht_hashtable_delete_ind(pht_hashtable_t *ht, int hash, void (*dtor_value)(void *));
void *pht_hashtable_search(pht_hashtable_t *ht, pht_string_t *key);
void *pht_hashtable_search_ind(pht_hashtable_t *ht, int hash);
pht_string_t *pht_hashtable_key_fetch(pht_hashtable_t *ht, pht_string_t *key);
void pht_hashtable_update(pht_hashtable_t *ht, pht_string_t *key, void *value);
void pht_hashtable_update_ind(pht_hashtable_t *ht, int hash, void *value);
void pht_hashtable_destroy(pht_hashtable_t *ht, void (*dtor_value)(void *));
void pht_hashtable_to_zend_hashtable(HashTable *zht, pht_hashtable_t *pht);

#endif
