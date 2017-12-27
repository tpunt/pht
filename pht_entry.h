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

#ifndef PHT_ENTRY_H
#define PHT_ENTRY_H

#include "Zend/zend.h"
#include "php_pht.h"
#include "pht_general.h"

typedef struct _queue_obj_t queue_obj_t;
typedef struct _hashtable_obj_t hashtable_obj_t;

typedef struct _entry_t {
    int type;
    union {
        int boolean;
        int integer;
        double floating;
        pht_string_t string;
        zend_function *func;
        queue_obj_t *queue;
        hashtable_obj_t *hash_table;
        // array
        // object
    } val;
} entry_t;

#define PHT_SERIALISATION_FAILED -1
#define PHT_STORE_FUNC 100
#define PHT_QUEUE 101
#define PHT_HASH_TABLE 102

#define ENTRY_TYPE(s) (s)->type
#define ENTRY_STRING(s) (s)->val.string
#define ENTRY_LONG(s) (s)->val.integer
#define ENTRY_DOUBLE(s) (s)->val.floating
#define ENTRY_BOOL(s) (s)->val.boolean
#define ENTRY_FUNC(s) (s)->val.func
#define ENTRY_Q(s) (s)->val.queue
#define ENTRY_HT(s) (s)->val.hash_table

void pht_convert_entry_to_zval(zval *value, entry_t *s);
void pht_convert_zval_to_entry(entry_t *e, zval *value);
void pht_entry_delete(void *entry_void);
void pht_entry_delete_value(entry_t *entry);
void pht_entry_update(entry_t *entry, zval *value);
entry_t *create_new_entry(zval *value);

#endif
