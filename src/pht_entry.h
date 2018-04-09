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

#ifndef PHT_ENTRY_H
#define PHT_ENTRY_H

#include "Zend/zend.h"

#include "src/pht_string.h"
#include "src/classes/queue.h"
#include "src/classes/hashtable.h"
#include "src/classes/vector.h"
#include "src/classes/atomic_integer.h"

typedef struct _pht_entry_t {
    int type;
    union {
        int boolean;
        int integer;
        double floating;
        pht_string_t string;
        zend_function *func;
        queue_obj_internal_t *queue;
        hashtable_obj_internal_t *hash_table;
        vector_obj_internal_t *vector;
        atomic_integer_obj_internal_t *atomic_integer;
        // array
        // object
    } val;
} pht_entry_t;

#define PHT_STORE_FUNC 100
#define PHT_QUEUE 101
#define PHT_HASH_TABLE 102
#define PHT_VECTOR 103
#define PHT_ATOMIC_INTEGER 104

#define PHT_ENTRY_TYPE(s) (s)->type
#define PHT_ENTRY_STRING(s) (s)->val.string
#define PHT_ENTRY_LONG(s) (s)->val.integer
#define PHT_ENTRY_DOUBLE(s) (s)->val.floating
#define PHT_ENTRY_BOOL(s) (s)->val.boolean
#define PHT_ENTRY_FUNC(s) (s)->val.func
#define PHT_ENTRY_Q(s) (s)->val.queue
#define PHT_ENTRY_HT(s) (s)->val.hash_table
#define PHT_ENTRY_V(s) (s)->val.vector
#define PHT_ENTRY_AI(s) (s)->val.atomic_integer

void pht_convert_entry_to_zval(zval *value, pht_entry_t *s);
int pht_convert_zval_to_entry(pht_entry_t *e, zval *value);
void pht_entry_delete(void *entry_void);
void pht_entry_delete_value(pht_entry_t *entry);
pht_entry_t *pht_create_entry_from_zval(zval *value);

#endif
