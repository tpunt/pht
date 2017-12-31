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

#include <main/php.h>
#include <main/php_main.h>
#include <Zend/zend_API.h>
#include <Zend/zend_exceptions.h>
#include <main/SAPI.h>

#include "php_pht.h"
#include "src/pht_copy.h"
#include "src/pht_debug.h"
#include "src/classes/thread.h"

extern zend_class_entry *Runnable_ce;

zend_object_handlers thread_handlers;
zend_class_entry *Thread_ce;
threads_t threads;

void task_free(task_t *task)
{
    pht_str_free(&task->class_name);

    if (task->class_ctor_argc) {
        for (int i = 0; i < task->class_ctor_argc; ++i) {
            pht_entry_delete_value(task->class_ctor_args + i);
        }

        free(task->class_ctor_args);
    }

    free(task);
}

void thread_init(thread_obj_t *thread, int tid)
{
    thread->tid = tid;
    thread->status = UNDER_CONSTRUCTION;
    thread->parent_thread_ls = TSRMLS_CACHE;
    pht_queue_init(&thread->tasks);
    pthread_mutex_init(&thread->lock, NULL);
}

void thread_destroy(thread_obj_t *thread)
{
    pthread_mutex_destroy(&thread->lock);
}

int aquire_thread_id(void)
{
    pthread_mutex_lock(&threads.lock);

    if (threads.used == threads.size) {
        threads.size <<= 1;
        threads.thread_table = realloc(threads.thread_table, threads.size  * sizeof(thread_obj_t *));
    }

    int tid = threads.used++;

    pthread_mutex_unlock(&threads.lock);

    return tid;
}

void handle_tasks(thread_obj_t *thread)
{
    // @todo mutex lock?
    while (thread->status != DESTROYED || thread->tasks.size) {
        task_t *task = pht_queue_pop(&thread->tasks);

        if (!task) {
            continue;
        }

        zend_string *ce_name = zend_string_init(task->class_name.val, task->class_name.len, 0);
        zend_class_entry *ce = zend_fetch_class_by_name(ce_name, NULL, ZEND_FETCH_CLASS_DEFAULT | ZEND_FETCH_CLASS_EXCEPTION);
        zend_function *constructor, *run;
        zval zobj;

        if (object_init_ex(&zobj, ce) != SUCCESS) {
            // @todo this will throw an exception in the new thread, rather than at
            // the call site. This doesn't even have an execution context - how
            // should it behave?
            zend_throw_exception_ex(zend_ce_exception, 0, "Failed to create Runnable object from class '%s'\n", ZSTR_VAL(ce_name));
            task_free(task);
            goto finish;
        }

        constructor = Z_OBJ_HT(zobj)->get_constructor(Z_OBJ(zobj));

        /*
        This is done here, rather than in the constructor branch, because if
        a HT or queue is passed in as an argument to Thread::addTask, then
        its reference count will be incremented. If no constructor is declared,
        however, then the constructor branch will never be hit, the
        corresponding HT or queue object will not be constructed, and so the
        reference count will not be decremented again. So we always convert
        serialised entries to their zvals to prevent this issue.
        */


        if (constructor) {
            int result;
            zval retval, zargs[task->class_ctor_argc];
            zend_fcall_info fci;

            fci.size = sizeof(fci);
            fci.object = Z_OBJ(zobj);
            fci.retval = &retval;
            fci.param_count = task->class_ctor_argc;
            fci.params = zargs;
            fci.no_separation = 1;
            // @todo doesn't have to be __construct (could be class name instead)
            ZVAL_STRINGL(&fci.function_name, "__construct", sizeof("__construct") - 1);

            for (int i = 0; i < task->class_ctor_argc; ++i) {
                pht_convert_entry_to_zval(zargs + i, task->class_ctor_args + i);
            }

            result = zend_call_function(&fci, NULL);

            if (result == FAILURE) {
                if (!EG(exception)) {
                    // @todo same exception throwing problem and constructor name as above?
                    zend_error_noreturn(E_CORE_ERROR, "Couldn't execute method %s%s%s", ZSTR_VAL(ce_name), "::", "__construct");
                    zval_dtor(&fci.function_name);
                    task_free(task);
                    goto finish;
                }
            }

            for (int i = 0; i < task->class_ctor_argc; ++i) {
                zval_dtor(zargs + i);
            }

            zval_dtor(&fci.function_name);
            // dtor on retval?
        }

        task_free(task);

        int result;
        zval retval;
        zend_fcall_info fci;

        fci.size = sizeof(fci);
        fci.object = Z_OBJ(zobj);
        fci.retval = &retval;
        fci.param_count = 0;
        fci.params = NULL;
        fci.no_separation = 1;
        ZVAL_STRINGL(&fci.function_name, "run", sizeof("run") - 1);

        result = zend_call_function(&fci, NULL);

        if (result == FAILURE) {
            if (!EG(exception)) {
                // same as problem above?
                zend_error_noreturn(E_CORE_ERROR, "Couldn't execute method %s%s%s", ZSTR_VAL(ce_name), "::", "run");
            }
        }

        zval_dtor(&fci.function_name);

finish:
        zend_string_free(ce_name);
        zval_ptr_dtor(&zobj);
    }
}

void *worker_function(thread_obj_t *thread)
{
    thread->id = (ulong) pthread_self();
    thread->ls = ts_resource(0);

    TSRMLS_CACHE_UPDATE();

    PHT_ZG(parent_thread_ls) = thread->parent_thread_ls;
    SG(server_context) = PHT_SG(PHT_ZG(parent_thread_ls), server_context);
    SG(sapi_started) = 0;

    PG(expose_php) = 0;
    PG(auto_globals_jit) = 0;

    php_request_startup();
    copy_execution_context();

    pthread_mutex_lock(&thread->lock);
    if (thread->status == UNDER_CONSTRUCTION) {
        thread->status = ACTIVE;
    }
    pthread_mutex_unlock(&thread->lock);

    handle_tasks(thread);

    // @todo clean up all undone tasks

    PG(report_memleaks) = 0;

    php_request_shutdown(NULL);

    ts_free_thread();

    pthread_exit(NULL);
}

static zend_object *thread_ctor(zend_class_entry *entry)
{
    thread_obj_t *thread = ecalloc(1, sizeof(thread_obj_t) + zend_object_properties_size(entry));
    int tid = aquire_thread_id();

    thread_init(thread, tid);

    zend_object_std_init(&thread->obj, entry);
    object_properties_init(&thread->obj, entry);

    thread->obj.handlers = &thread_handlers;

    pthread_mutex_lock(&threads.lock);
    threads.thread_table[tid] = thread;
    pthread_mutex_unlock(&threads.lock);

    return &thread->obj;
}

void th_free_obj(zend_object *obj)
{
    thread_obj_t *thread = (thread_obj_t *)((char *)obj - obj->handlers->offset);

    thread_destroy(thread);
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
            pht_convert_zval_to_entry(task->class_ctor_args + i, args + i);
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

    memcpy(&thread_handlers, zh, sizeof(zend_object_handlers));

    thread_handlers.offset = XtOffsetOf(thread_obj_t, obj);
    thread_handlers.free_obj = th_free_obj;

    threads.size = 16;
    threads.used = 0;
    threads.thread_table = malloc(threads.size * sizeof(thread_obj_t *));
    pthread_mutex_init(&threads.lock, NULL);
}

void pht_thread_mshutdown(void)
{
    pthread_mutex_destroy(&threads.lock);
    free(threads.thread_table);
}
