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

#ifndef PHT_QUEUE_CLASS_H
#define PHT_QUEUE_CLASS_H

#include <main/php.h>
#include <stdint.h>
#include <pthread.h>

#include "src/ds/pht_queue.h"

typedef struct _queue_obj_internal_t {
    pht_queue_t queue;
    pthread_mutex_t lock;
    uint32_t refcount;
    zend_ulong vn;
} queue_obj_internal_t;

typedef struct _queue_obj_t {
    queue_obj_internal_t *qoi;
    zend_ulong vn;
    zend_object obj;
} queue_obj_t;

void qoi_free(queue_obj_internal_t *qoi);
void queue_ce_init(void);

extern zend_class_entry *Queue_ce;

#endif
