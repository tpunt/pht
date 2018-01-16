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
#include <Zend/zend_compile.h>

#include "php_pht.h"
#include "src/pht_copy.h"
#include "src/pht_thread.h"

void task_delete(void *task_void)
{
    task_t *task = task_void;

    pht_str_free(&task->class_name);

    if (task->class_ctor_argc) {
        for (int i = 0; i < task->class_ctor_argc; ++i) {
            pht_entry_delete_value(task->class_ctor_args + i);
        }

        free(task->class_ctor_args);
    }

    free(task);
}

void thread_init(thread_obj_t *thread, pht_thread_type_t type)
{
    thread->status = UNDER_CONSTRUCTION;
    thread->type = type;
    thread->parent_thread_ls = TSRMLS_CACHE;

    pht_queue_init(&thread->tasks, task_delete);
    pthread_mutex_init(&thread->lock, NULL);
}

void th_free_obj(zend_object *obj)
{
    thread_obj_t *thread = (thread_obj_t *)((char *)obj - obj->handlers->offset);

    zend_hash_index_del(&PHT_ZG(child_threads), (zend_ulong)thread);
}

void thread_join_destroy(zval *zthread)
{
    thread_obj_t *thread = Z_PTR_P(zthread);

    thread->status = DESTROYED;

    pthread_join(thread->thread, NULL);

    pthread_mutex_destroy(&thread->lock);

    // technically, thread should leak here without an efree, but because this
    // function is indirectly invoked in the free_obj callback, we cannot efree
    // here. This only occurs for when threads are not explicitly join()'ed
}

void handle_file_thread_task(thread_obj_t *thread)
{
    task_t *task = pht_queue_pop(&thread->tasks);
    zend_file_handle zfd;

    zfd.type = ZEND_HANDLE_FILENAME;
    zfd.filename = PHT_STRV(task->class_name);
    zfd.free_filename = 0;
    zfd.opened_path = NULL;

    php_execute_script(&zfd);
}

void handle_thread_tasks(thread_obj_t *thread)
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
            task_delete(task);
            goto finish;
        }

        constructor = Z_OBJ_HT(zobj)->get_constructor(Z_OBJ(zobj));

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
                    task_delete(task);
                    goto finish;
                }
            }

            for (int i = 0; i < task->class_ctor_argc; ++i) {
                zval_dtor(zargs + i);
            }

            zval_dtor(&fci.function_name);
            // dtor on retval?
        }

        task_delete(task);

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

    if (thread->type != FILE_THREAD) {
        copy_execution_context();
    } else {
        task_t *task = pht_queue_front(&thread->tasks);
        zval thread_args, element;
        zend_execute_data *ced = EG(current_execute_data);

        array_init(&thread_args);

        for (int i = 0; i < task->class_ctor_argc; ++i) {
            pht_convert_entry_to_zval(&element, task->class_ctor_args + i);
            zend_hash_next_index_insert_new(Z_ARR(thread_args), &element);
        }

        zend_hash_str_add(&EG(symbol_table), "_THREAD", sizeof("_THREAD") - 1, &thread_args);
    }

    pthread_mutex_lock(&thread->lock);
    if (thread->status == UNDER_CONSTRUCTION) {
        thread->status = ACTIVE;
    }
    pthread_mutex_unlock(&thread->lock);

    if (thread->type != FILE_THREAD) {
        handle_thread_tasks(thread);
    } else {
        handle_file_thread_task(thread);
    }

    // @todo clean up all undone tasks

    PG(report_memleaks) = 0;

    php_request_shutdown(NULL);

    ts_free_thread();

    pthread_exit(NULL);
}
