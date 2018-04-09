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

#ifndef PHT_VECTOR_CLASS_H
#define PHT_VECTOR_CLASS_H

#include "src/ds/pht_vector.h"

typedef struct _vector_obj_internal_t {
    pht_vector_t vector;
    pthread_mutex_t lock;
    uint32_t refcount;
    zend_ulong vn;
} vector_obj_internal_t;

typedef struct _vector_obj_t {
    vector_obj_internal_t *voi;
    zend_ulong vn;
    zend_object obj;
} vector_obj_t;

void voi_free(vector_obj_internal_t *voi);
void vector_ce_init(void);

extern zend_class_entry *Vector_ce;

#endif
