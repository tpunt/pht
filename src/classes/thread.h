/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2017 The PHP Group                                |
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

#ifndef PHT_THREAD_CLASS_H
#define PHT_THREAD_CLASS_H

#include <main/php.h>
#include <pthread.h>

#include "src/pht_entry.h"
#include "src/ds/pht_queue.h"

typedef struct _task_t {
    pht_string_t class_name;
    int class_ctor_argc;
    pht_entry_t *class_ctor_args;
} task_t;

typedef enum _status_t {
    UNDER_CONSTRUCTION,
    ACTIVE,
    FINISHED,
    DESTROYED // JOINED?
} status_t;

typedef struct _thread_obj_t {
    pthread_t thread; // must be first member
    int tid; // use thread as global in tls instead?
    zend_ulong id; // local storage ID used to fetch local storage data
    pthread_mutex_t lock;
    status_t status;
    pht_queue_t tasks;
    void*** ls; // pointer to local storage in TSRM
    void*** parent_thread_ls;
    zend_object obj;
} thread_obj_t;

typedef struct _threads_t {
    thread_obj_t **thread_table;
    int size;
    int used;
    pthread_mutex_t lock;
} threads_t;

void thread_ce_init(void);
void pht_thread_mshutdown(void);

#endif
