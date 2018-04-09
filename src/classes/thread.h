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

#ifndef PHT_THREAD_CLASS_H
#define PHT_THREAD_CLASS_H

#include <main/php.h>
#include <pthread.h>

#include "src/pht_entry.h"
#include "src/ds/pht_queue.h"

typedef struct _class_task_t {
    pht_string_t name;
    int ctor_argc;
    pht_entry_t *ctor_args;
} class_task_t;

typedef struct _function_task_t {
    pht_entry_t fn;
    int argc;
    pht_entry_t *args;
} function_task_t;

typedef struct _file_task_t {
    pht_string_t name;
    int argc;
    pht_entry_t *args;
} file_task_t;

typedef enum _pht_task_type_t {
    CLASS_TASK,
    FUNCTION_TASK,
    FILE_TASK
} pht_task_type_t;

typedef struct _task_t {
    union {
        class_task_t class;
        function_task_t function;
        file_task_t file;
    } t;
    pht_task_type_t type;
} task_t;

typedef enum _status_t {
    NOT_STARTED,
    STARTING_UP,
    STARTED,
    JOINED
} status_t;

typedef struct _thread_obj_t {
    pthread_t thread; // must be first member
    zend_ulong id; // local storage ID used to fetch local storage data
    pthread_mutex_t lock;
    status_t status;
    pht_queue_t tasks;
    void*** ls; // pointer to local storage in TSRM
    void*** parent_thread_ls;
    zend_object obj;
} thread_obj_t;

void thread_join_destroy(zval *zthread);
void thread_ce_init(void);

#endif
