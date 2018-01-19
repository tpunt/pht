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

#include <main/SAPI.h>
#include <Zend/zend_interfaces.h>

#include "php_pht.h"
#include "src/pht_debug.h"
#include "src/pht_thread.h"
#include "src/classes/thread.h"

extern zend_class_entry *Runnable_ce;

zend_object_handlers thread_handlers;
zend_class_entry *Thread_ce;

static zend_object *thread_ctor(zend_class_entry *entry)
{
    thread_obj_t *thread = ecalloc(1, sizeof(thread_obj_t) + zend_object_properties_size(entry));

    thread_init(thread, CLASS_THREAD);

    zend_object_std_init(&thread->obj, entry);
    object_properties_init(&thread->obj, entry);

    thread->obj.handlers = &thread_handlers;

    zend_hash_index_add_ptr(&PHT_ZG(child_threads), (zend_ulong)thread, thread);

    return &thread->obj;
}

ZEND_BEGIN_ARG_INFO_EX(Thread_add_task_arginfo, 0, 0, 1)
    ZEND_ARG_INFO(0, class_name)
ZEND_END_ARG_INFO()

PHP_METHOD(Thread, addTask)
{
    zend_class_entry *ce = Runnable_ce;
    zval *args;
    int argc = 0;

    ZEND_PARSE_PARAMETERS_START(1, -1)
        Z_PARAM_CLASS(ce)
        Z_PARAM_VARIADIC('*', args, argc)
    ZEND_PARSE_PARAMETERS_END();

    // By loading the class entry here, we ensure that it exists before
    // asynchronously creating the underlying Runnable object. We can simply
    // discard the ce here and use only the ce name now

    thread_obj_t *thread = (thread_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);
    task_t *task = malloc(sizeof(task_t));

    pht_str_update(&task->class_name, ZSTR_VAL(ce->name), ZSTR_LEN(ce->name));
    task->class_ctor_argc = argc;

    if (argc) {
        task->class_ctor_args = malloc(sizeof(pht_entry_t) * argc);

        for (int i = 0; i < argc; ++i) {
            if (!pht_convert_zval_to_entry(task->class_ctor_args + i, args + i)) {
                zend_throw_error(NULL, "Failed to serialise argument %d of Thread::addTask()", i + 1);

                for (int i2 = 0; i2 < i; ++i2) {
                    pht_entry_delete_value(task->class_ctor_args + i2);
                }

                free(task->class_ctor_args);
                return;
            }
        }
    } else {
        task->class_ctor_args = NULL;
    }

    pht_queue_push(&thread->tasks, task);
}

ZEND_BEGIN_ARG_INFO_EX(Thread_start_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Thread, start)
{
    thread_obj_t *thread = (thread_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    pthread_create((pthread_t *)thread, NULL, (void *)worker_function, thread);
}

ZEND_BEGIN_ARG_INFO_EX(Thread_join_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Thread, join)
{
    thread_obj_t *thread = (thread_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    thread->status = DESTROYED;

    pthread_join(thread->thread, NULL);
}

ZEND_BEGIN_ARG_INFO_EX(Thread_task_count_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Thread, taskCount)
{
    thread_obj_t *thread = (thread_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    RETVAL_LONG(thread->tasks.size);
}

zend_function_entry Thread_methods[] = {
    PHP_ME(Thread, addTask, Thread_add_task_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(Thread, start, Thread_start_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(Thread, join, Thread_join_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(Thread, taskCount, Thread_task_count_arginfo, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

void thread_ce_init(void)
{
    zend_class_entry ce;
    zend_object_handlers *zh = zend_get_std_object_handlers();

    INIT_CLASS_ENTRY(ce, "Thread", Thread_methods);
    Thread_ce = zend_register_internal_class(&ce);
    Thread_ce->create_object = thread_ctor;
    Thread_ce->serialize = zend_class_serialize_deny;
    Thread_ce->unserialize = zend_class_unserialize_deny;

    memcpy(&thread_handlers, zh, sizeof(zend_object_handlers));

    thread_handlers.offset = XtOffsetOf(thread_obj_t, obj);
    thread_handlers.free_obj = th_free_obj;
}
