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

#ifndef PHT_ATOMIC_INTEGER_CLASS_H
#define PHT_ATOMIC_INTEGER_CLASS_H

#include <main/php.h>
#include <stdint.h>
#include <pthread.h>

typedef struct _atomic_integer_obj_internal_t {
    zend_long value;
    pthread_mutex_t lock;
    uint32_t refcount;
} atomic_integer_obj_internal_t;

typedef struct _atomic_integer_obj_t {
    atomic_integer_obj_internal_t *aioi;
    zend_object obj;
} atomic_integer_obj_t;

extern zend_class_entry *AtomicInteger_ce;

void aioi_free(atomic_integer_obj_internal_t *aioi);
void atomic_integer_ce_init(void);

#endif
