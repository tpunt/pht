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

#include <main/php.h>
#include <main/php_main.h>
#include <main/SAPI.h>
#include <Zend/zend_API.h>
#include <Zend/zend_closures.h>
#include <Zend/zend_compile.h>
#include <Zend/zend_exceptions.h>
#include <Zend/zend_interfaces.h>

#include "php_pht.h"
#include "src/pht_copy.h"
#include "src/pht_debug.h"
#include "src/classes/thread.h"

extern zend_class_entry *Runnable_ce;

zend_object_handlers thread_handlers;
zend_class_entry *Thread_ce;

void class_task_delete(class_task_t *class_task)
{
    pht_str_free(&class_task->name);

    if (class_task->ctor_argc) {
        for (int i = 0; i < class_task->ctor_argc; ++i) {
            pht_entry_delete_value(class_task->ctor_args + i);
        }

        free(class_task->ctor_args);
    }
}

void file_task_delete(file_task_t *file_task)
{
    pht_str_free(&file_task->name);

    if (file_task->argc) {
        for (int i = 0; i < file_task->argc; ++i) {
            pht_entry_delete_value(file_task->args + i);
        }

        free(file_task->args);
    }
}

void function_task_delete(function_task_t *function_task)
{
    pht_entry_delete_value(&function_task->fn);

    if (function_task->argc) {
        for (int i = 0; i < function_task->argc; ++i) {
            pht_entry_delete_value(function_task->args + i);
        }

        free(function_task->args);
    }
}

void task_delete(void *task_void)
{
    task_t *task = task_void;

    switch (task->type) {
        case CLASS_TASK:
            class_task_delete(&task->t.class);
            break;
        case FUNCTION_TASK:
            function_task_delete(&task->t.function);
            break;
        case FILE_TASK:
            file_task_delete(&task->t.file);
    }

    free(task);
}

void thread_init(thread_obj_t *thread)
{
    thread->status = NOT_STARTED;
    thread->parent_thread_ls = TSRMLS_CACHE;

    pht_queue_init(&thread->tasks, task_delete);
    pthread_mutex_init(&thread->lock, NULL);
}

void th_free_obj(zend_object *obj)
{
    if (EG(exit_status)) {
        // We don't remove the object from PHT_ZG(child_threads), as this causes
        // problems when ZE bails out due to an exception, causing the child_threads
        // HT to be destroyed before this object's function has been invoked.
        return;
    }

    thread_obj_t *thread = (thread_obj_t *)((char *)obj - obj->handlers->offset);

    zend_hash_index_del(&PHT_ZG(child_threads), (zend_ulong)thread);
}

void thread_join_destroy(zval *zthread)
{
    thread_obj_t *thread = Z_PTR_P(zthread);

    if (thread->status == NOT_STARTED) {
        return;
    }

    pthread_mutex_lock(&thread->lock);
    thread->status = JOINED;
    pthread_mutex_unlock(&thread->lock);

    pthread_join(thread->thread, NULL);

    pthread_mutex_destroy(&thread->lock);

    // technically, thread should leak here without an efree, but because this
    // function is indirectly invoked in the free_obj callback, we cannot efree
    // here. This only occurs for when threads are not explicitly join()'ed
}

void handle_class_task(class_task_t *class_task)
{
    zend_string *ce_name = zend_string_init(PHT_STRV(class_task->name), PHT_STRL(class_task->name), 0);
    zend_class_entry *ce = zend_fetch_class_by_name(ce_name, NULL, ZEND_FETCH_CLASS_DEFAULT | ZEND_FETCH_CLASS_EXCEPTION);
    zend_function *constructor;
    zval zobj;

    if (object_init_ex(&zobj, ce) != SUCCESS) {
        // @todo this will throw an exception in the new thread, rather than at
        // the call site. This doesn't even have an execution context - how
        // should it behave?
        zend_throw_exception_ex(zend_ce_exception, 0, "Failed to create Runnable object from class '%s'\n", ZSTR_VAL(ce_name));
        zend_string_free(ce_name);
        return;
    }

    constructor = Z_OBJ_HT(zobj)->get_constructor(Z_OBJ(zobj));

    if (constructor) {
        int result;
        zval retval, *zargs = emalloc(sizeof(zval) * class_task->ctor_argc); // VLAs not supported by VC15
        zend_fcall_info fci;

        fci.size = sizeof(fci);
        fci.object = Z_OBJ(zobj);
        fci.retval = &retval;
        fci.param_count = class_task->ctor_argc;
        fci.params = zargs;
        fci.no_separation = 1;
        // @todo doesn't have to be __construct (could be class name instead)
        ZVAL_INTERNED_STR(&fci.function_name, common_strings.__construct);

        for (int i = 0; i < class_task->ctor_argc; ++i) {
            pht_convert_entry_to_zval(zargs + i, class_task->ctor_args + i);
        }

        result = zend_call_function(&fci, NULL);

        for (int i = 0; i < class_task->ctor_argc; ++i) {
            zval_dtor(zargs + i);
        }

        efree(zargs);

        // dtor retval too?

        if (result == FAILURE) {
            if (!EG(exception)) {
                // @todo same exception throwing problem and constructor name as above?
                zend_error_noreturn(E_CORE_ERROR, "Couldn't execute method %s%s%s", ZSTR_VAL(ce_name), "::", "__construct");
                goto finish;
            }
        }
    }

    int result;
    zval retval;
    zend_fcall_info fci;

    fci.size = sizeof(fci);
    fci.object = Z_OBJ(zobj);
    fci.retval = &retval;
    fci.param_count = 0;
    fci.params = NULL;
    fci.no_separation = 1;
    ZVAL_INTERNED_STR(&fci.function_name, common_strings.run);

    result = zend_call_function(&fci, NULL);

    if (result == FAILURE) {
        if (!EG(exception)) {
            // same as problem above?
            zend_error_noreturn(E_CORE_ERROR, "Couldn't execute method %s%s%s", ZSTR_VAL(ce_name), "::", "run");
        }
    }

finish:
    zend_string_free(ce_name);
    zval_ptr_dtor(&zobj);
}

void handle_function_task(function_task_t *function_task)
{
    zval fn, retval, *params = NULL;

    pht_convert_entry_to_zval(&fn, &function_task->fn);

    if (function_task->argc) {
        params = emalloc(sizeof(zval) * function_task->argc);

        for (int i = 0; i < function_task->argc; ++i) {
            pht_convert_entry_to_zval(params + i, function_task->args + i);
        }
    }

    switch (Z_TYPE(fn)) {
        case IS_STRING:
        case IS_ARRAY:
        case IS_OBJECT:
            call_user_function(CG(function_table), NULL, &fn, &retval, function_task->argc, params);
            break;
        default:
            ZEND_ASSERT(0);
    }
}

void handle_file_task(file_task_t *file_task)
{
    zval file_args, element, *file_args2 = zend_hash_find(&EG(symbol_table), common_strings._THREAD);

    if (file_args2) {
        zend_hash_clean(Z_ARR_P(file_args2));
        ZVAL_ARR(&file_args, Z_ARR_P(file_args2));
    } else {
        array_init(&file_args);
    }

    for (int i = 0; i < file_task->argc; ++i) {
        pht_convert_entry_to_zval(&element, file_task->args + i);
        zend_hash_next_index_insert_new(Z_ARR(file_args), &element);
    }

    zend_hash_add(&EG(symbol_table), common_strings._THREAD, &file_args);

    zend_file_handle zfd;

    zfd.type = ZEND_HANDLE_FILENAME;
    zfd.filename = PHT_STRV(file_task->name);
    zfd.free_filename = 0;
    zfd.opened_path = NULL;

    zend_compile_file = compile_file;

    php_execute_script(&zfd);
}

void handle_thread_tasks(thread_obj_t *thread)
{
    // @todo mutex lock?
    while (thread->status != JOINED || thread->tasks.size) {
        pthread_mutex_lock(&thread->lock);
        task_t *task = pht_queue_pop(&thread->tasks);
        pthread_mutex_unlock(&thread->lock);

        if (!task) {
            continue;
        }

        switch (task->type) {
            case CLASS_TASK:
                handle_class_task(&task->t.class);
                break;
            case FUNCTION_TASK:
                handle_function_task(&task->t.function);
                break;
            case FILE_TASK:
                handle_file_task(&task->t.file);
        }

        task_delete(task);
    }
}

void *worker_function(thread_obj_t *thread)
{
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
    if (thread->status == STARTING_UP) { // it could also be JOINED
        thread->status = STARTED;
    }
    pthread_mutex_unlock(&thread->lock);

    handle_thread_tasks(thread);
    // @todo clean up all undone tasks

    PG(report_memleaks) = 0;

    php_request_shutdown(NULL);

    ts_free_thread();

    pthread_exit(NULL);
}

static zend_object *thread_ctor(zend_class_entry *entry)
{
    thread_obj_t *thread = ecalloc(1, sizeof(thread_obj_t) + zend_object_properties_size(entry));

    thread_init(thread);

    zend_object_std_init(&thread->obj, entry);
    object_properties_init(&thread->obj, entry);

    thread->obj.handlers = &thread_handlers;

    zend_hash_index_add_ptr(&PHT_ZG(child_threads), (zend_ulong)thread, thread);

    return &thread->obj;
}

ZEND_BEGIN_ARG_INFO_EX(Thread_add_class_task_arginfo, 0, 0, 1)
    ZEND_ARG_INFO(0, class_name)
ZEND_END_ARG_INFO()

PHP_METHOD(Thread, addClassTask)
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

    task->type = CLASS_TASK;
    pht_str_update(&task->t.class.name, ZSTR_VAL(ce->name), ZSTR_LEN(ce->name));
    task->t.class.ctor_argc = argc;

    if (argc) {
        task->t.class.ctor_args = malloc(sizeof(pht_entry_t) * argc);

        for (int i = 0; i < argc; ++i) {
            if (!pht_convert_zval_to_entry(task->t.class.ctor_args + i, args + i)) {
                zend_throw_error(NULL, "Failed to serialise argument %d of Thread::addClassTask()", i + 1);

                for (int i2 = 0; i2 < i; ++i2) {
                    pht_entry_delete_value(task->t.class.ctor_args + i2);
                }

                free(task->t.class.ctor_args);
                return;
            }
        }
    } else {
        task->t.class.ctor_args = NULL;
    }

    pthread_mutex_lock(&thread->lock);
    pht_queue_push(&thread->tasks, task);
    pthread_mutex_unlock(&thread->lock);
}

ZEND_BEGIN_ARG_INFO_EX(Thread_add_function_task_arginfo, 0, 0, 1)
    ZEND_ARG_INFO(0, callable)
ZEND_END_ARG_INFO()

PHP_METHOD(Thread, addFunctionTask)
{
    zval *args, *zcallable;
    int argc = 0;

    ZEND_PARSE_PARAMETERS_START(1, -1)
        Z_PARAM_ZVAL(zcallable)
        Z_PARAM_VARIADIC('*', args, argc)
    ZEND_PARSE_PARAMETERS_END();

    if (!zend_is_callable(zcallable, 0, NULL)) {
        zend_throw_error(NULL, "Invalid callable array given");
        return;
    }

    thread_obj_t *thread = (thread_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);
    task_t *task = malloc(sizeof(task_t));

    while (Z_ISREF_P(zcallable)) {
        zcallable = Z_REFVAL_P(zcallable);
    }

    if (Z_TYPE_P(zcallable) != IS_ARRAY) {
        pht_convert_zval_to_entry(&task->t.function.fn, zcallable);
    } else {
        zval *obj = zend_hash_index_find(Z_ARR_P(zcallable), 0);

        ZVAL_DEREF(obj);

        if (Z_TYPE_P(obj) == IS_STRING) {
            pht_convert_zval_to_entry(&task->t.function.fn, zcallable);
        } else {
            zval new_array, ce_name, *method = zend_hash_index_find(Z_ARR_P(zcallable), 1);

            ZVAL_DEREF(method);
            ZVAL_NEW_ARR(&new_array);
            zend_hash_init(Z_ARR(new_array), 2, NULL, ZVAL_PTR_DTOR, 0);
            ZVAL_STR(&ce_name, Z_OBJCE_P(obj)->name);

            zend_hash_next_index_insert_new(Z_ARR(new_array), &ce_name);
            zend_hash_next_index_insert_new(Z_ARR(new_array), method);

            pht_convert_zval_to_entry(&task->t.function.fn, &new_array);
            zval_dtor(&new_array);
        }
    }

    task->type = FUNCTION_TASK;
    task->t.function.argc = argc;

    if (argc) {
        task->t.function.args = malloc(sizeof(pht_entry_t) * argc);

        for (int i = 0; i < argc; ++i) {
            if (!pht_convert_zval_to_entry(task->t.function.args + i, args + i)) {
                zend_throw_error(NULL, "Failed to serialise argument %d of Thread::addClassTask()", i + 1);

                for (int i2 = 0; i2 < i; ++i2) {
                    pht_entry_delete_value(task->t.function.args + i2);
                }

                free(task->t.function.args);
                return;
            }
        }
    } else {
        task->t.function.args = NULL;
    }

    pthread_mutex_lock(&thread->lock);
    pht_queue_push(&thread->tasks, task);
    pthread_mutex_unlock(&thread->lock);
}

ZEND_BEGIN_ARG_INFO_EX(Thread_add_file_task_arginfo, 0, 0, 1)
ZEND_ARG_INFO(0, filename)
ZEND_END_ARG_INFO()

PHP_METHOD(Thread, addFileTask)
{
    zend_string *filename, *resolved_path;
    zval *args;
    int argc = 0;

    ZEND_PARSE_PARAMETERS_START(1, -1)
        Z_PARAM_PATH_STR(filename)
        Z_PARAM_VARIADIC('*', args, argc)
    ZEND_PARSE_PARAMETERS_END();

    thread_obj_t *thread = (thread_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);
    task_t *task = malloc(sizeof(task_t));

    task->type = FILE_TASK;

    resolved_path = zend_resolve_path(ZSTR_VAL(filename), ZSTR_LEN(filename));

    if (resolved_path) {
        pht_str_update(&task->t.file.name, ZSTR_VAL(resolved_path), ZSTR_LEN(resolved_path));
        zend_string_release(resolved_path);
    } else {
        pht_str_update(&task->t.file.name, ZSTR_VAL(filename), ZSTR_LEN(filename));
    }

    if (VCWD_ACCESS(PHT_STRV(task->t.file.name), F_OK) != 0) {
        zend_throw_error(NULL, "The file '%s' does not exist", PHT_STRV(task->t.file.name));
        free(PHT_STRV(task->t.file.name));
        free(task);
        return;
    }

    if (VCWD_ACCESS(PHT_STRV(task->t.file.name), R_OK) != 0) {
        zend_throw_error(NULL, "The file '%s' is not readable", PHT_STRV(task->t.file.name));
        free(PHT_STRV(task->t.file.name));
        free(task);
        return;
    }

    task->t.file.argc = argc;

    if (argc) {
        task->t.file.args = malloc(sizeof(pht_entry_t) * argc);

        for (int i = 0; i < argc; ++i) {
            if (!pht_convert_zval_to_entry(task->t.file.args + i, args + i)) {
                zend_throw_error(NULL, "Failed to serialise argument %d of Thread::addClassTask()", i + 1);

                for (int i2 = 0; i2 < i; ++i2) {
                    pht_entry_delete_value(task->t.file.args + i2);
                }

                free(task->t.file.args);
                free(task);
                return;
            }
        }
    } else {
        task->t.file.args = NULL;
    }

    pthread_mutex_lock(&thread->lock);
    pht_queue_push(&thread->tasks, task);
    pthread_mutex_unlock(&thread->lock);
}

ZEND_BEGIN_ARG_INFO_EX(Thread_start_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Thread, start)
{
    thread_obj_t *thread = (thread_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    thread->status = STARTING_UP;

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

    thread->status = JOINED;

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
    PHP_ME(Thread, addClassTask, Thread_add_class_task_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(Thread, addFunctionTask, Thread_add_function_task_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(Thread, addFileTask, Thread_add_file_task_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(Thread, start, Thread_start_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(Thread, join, Thread_join_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(Thread, taskCount, Thread_task_count_arginfo, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

void thread_ce_init(void)
{
    zend_class_entry ce;
    zend_object_handlers *zh = zend_get_std_object_handlers();

    INIT_CLASS_ENTRY(ce, "pht\\Thread", Thread_methods);
    Thread_ce = zend_register_internal_class(&ce);
    Thread_ce->create_object = thread_ctor;
    Thread_ce->serialize = zend_class_serialize_deny;
    Thread_ce->unserialize = zend_class_unserialize_deny;

    memcpy(&thread_handlers, zh, sizeof(zend_object_handlers));

    thread_handlers.offset = XtOffsetOf(thread_obj_t, obj);
    thread_handlers.free_obj = th_free_obj;
}
