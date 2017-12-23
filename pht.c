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
  | Author:                                                              |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "ext/standard/info.h"
#include "main/SAPI.h"
#include "main/php_main.h"

#include "php_pht.h"
#include "pht_copy.h"

zend_object_handlers thread_handlers;
zend_object_handlers message_queue_handlers;

zend_class_entry *Threaded_ce;
zend_class_entry *Thread_ce;
zend_class_entry *MessageQueue_ce;

thread_t main_thread;
threads_t threads;

ZEND_DECLARE_MODULE_GLOBALS(pht)

int aquire_thread_id(void)
{
    pthread_mutex_lock(&threads.lock);

    if (threads.used == threads.size) {
        threads.size <<= 1;
        threads.thread_table = realloc(threads.thread_table, threads.size);
    }

    int tid = threads.used++;

    pthread_mutex_unlock(&threads.lock);

    return tid;
}

void thread_init(thread_t *thread, int tid)
{
    thread->tid = tid;
    thread->status = UNDER_CONSTRUCTION;
    queue_init(&thread->tasks);
    pthread_mutex_init(&thread->lock, NULL);
}

void thread_destroy(thread_t *thread)
{
    pthread_mutex_destroy(&thread->lock);
}

void free_message_queue_internal(message_queue_internal_t *mqi)
{
    pthread_mutex_destroy(&mqi->lock);

    while (mqi->messages) {
        message_t *next_message = mqi->messages->next;

        free(mqi->messages->message); // free entry_t
        free(mqi->messages); // free message_t
        mqi->messages = next_message;
    }

    free(mqi);
}

message_t *create_new_message(entry_t *entry)
{
    message_t *message = malloc(sizeof(message_t));

    message->message = entry;
    message->next = NULL;

    return message;
}

void *worker_function(thread_t *thread)
{
    // thread_offset = thread->offset;
    // thread = thread;
    thread->id = (ulong) pthread_self();
    thread->ls = ts_resource(0);

    TSRMLS_CACHE_UPDATE();

    SG(server_context) = PHT_SG(main_thread.ls, server_context);
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

    while (thread->status != DESTROYED) {
        task_t *task = dequeue(&thread->tasks);

        if (!task) {
            continue;
        }

        zend_string *ce_name = zend_string_init(task->class_name.val, task->class_name.len, 0);
        zend_class_entry *ce = zend_fetch_class_by_name(ce_name, NULL, ZEND_FETCH_CLASS_DEFAULT | ZEND_FETCH_CLASS_EXCEPTION);
        zend_function *constructor, *run;
        zval zobj, zargs[task->class_ctor_argc]; // goto cannot bypass VLAs...

        if (object_init_ex(&zobj, ce) != SUCCESS) {
            // @todo this will throw an exception in the new thread, rather than at
            // the call site. This doesn't even have an execution context - how
            // should it behave?
            zend_throw_exception_ex(zend_ce_exception, 0, "Failed to create threaded object from class '%s'\n", ZSTR_VAL(ce_name));
            goto finish;
        }

        // inc Z_OBJ(zobj) refcount ?

        // thread->threaded_object = Z_OBJ(zobj);

        constructor = Z_OBJ_HT(zobj)->get_constructor(Z_OBJ(zobj));

        /*
        This is done here, rather than in the constructor branch, because if
        a message queue is passed in as an argument to Thread::addTask, then
        its reference count will be incremented. If no constructor is declared,
        however, then the constructor branch will never be hit, the
        corresponding message queue object will not be constructed, and so the
        reference count will not be decremented again. So we always convert
        serialised entries to their zvals to prevent this issue.
        */
        for (int i = 0; i < task->class_ctor_argc; ++i) {
            pht_convert_entry_to_zval(zargs + i, task->class_ctor_args + i);
        }

        if (constructor) {
            int result;
            zval retval;
            zend_fcall_info fci;

            fci.size = sizeof(fci);
            fci.object = Z_OBJ(zobj);
            fci.retval = &retval;
            fci.param_count = task->class_ctor_argc;
            fci.params = zargs;
            fci.no_separation = 1;
            // @todo doesn't have to be __construct (could be class name instead)
            ZVAL_STRINGL(&fci.function_name, "__construct", sizeof("__construct") - 1);

            result = zend_call_function(&fci, NULL);

            if (result == FAILURE) {
                if (!EG(exception)) {
                    // @todo same exception throwing problem and constructor name as above?
                    zend_error_noreturn(E_CORE_ERROR, "Couldn't execute method %s%s%s", ZSTR_VAL(ce_name), "::", "__construct");
                    zval_dtor(&fci.function_name);
                    goto finish;
                }
            }

            zval_dtor(&fci.function_name);
            // dtor on retval?
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

        if (task->class_ctor_args) {
            free(task->class_ctor_args);
        }
        free(task);
    }

    PG(report_memleaks) = 0;

    php_request_shutdown(NULL);

    ts_free_thread();

    pthread_exit(NULL);
}

void message_queue_push(message_queue_internal_t *mqi, message_t *message)
{
    pthread_mutex_lock(&mqi->lock);

    if (!mqi->messages) {
        mqi->messages = message;
    } else {
        mqi->last_message->next = message;
    }

    mqi->last_message = message;

    pthread_mutex_unlock(&mqi->lock);
}

zend_bool message_queue_pop(message_queue_internal_t *mqi, zval *zmessage)
{
    message_t *emessage = NULL;

    pthread_mutex_lock(&mqi->lock);

    if (mqi->messages) {
        emessage = mqi->messages;
        mqi->messages = mqi->messages->next;

        if (!mqi->messages) {
            mqi->last_message = NULL;
        }
    }

    pthread_mutex_unlock(&mqi->lock);

    if (!emessage) {
        return 0;
    }

    zval_ptr_dtor(zmessage);
    pht_convert_entry_to_zval(zmessage, emessage->message);
    free(emessage->message);
    free(emessage);

    return 1;
}

static zend_object *thread_ctor(zend_class_entry *entry)
{
    thread_t *thread = ecalloc(1, sizeof(thread_t) + zend_object_properties_size(entry));
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
    thread_t *thread = (thread_t *)((char *)obj - obj->handlers->offset);

    thread_destroy(thread);
}

static zend_object *message_queue_ctor(zend_class_entry *entry)
{
    message_queue_t *message_queue = ecalloc(1, sizeof(message_queue_t) + zend_object_properties_size(entry));

    zend_object_std_init(&message_queue->obj, entry);
    object_properties_init(&message_queue->obj, entry);

    message_queue->obj.handlers = &message_queue_handlers;

    if (!PHT_ZG(skip_mqi_creation)) {
        message_queue_internal_t *mqi = calloc(1, sizeof(message_queue_internal_t));

        mqi->state = 0;
        mqi->refcount = 1;
        pthread_mutex_init(&mqi->lock, NULL);

        message_queue->mqi = mqi;
    }

    return &message_queue->obj;
}

void mqh_free_obj(zend_object *obj)
{
    message_queue_t *message_queue = (message_queue_t *)((char *)obj - obj->handlers->offset);

    pthread_mutex_lock(&message_queue->mqi->lock);
    --message_queue->mqi->refcount;
    pthread_mutex_unlock(&message_queue->mqi->lock);

    if (!message_queue->mqi->refcount) {
        free_message_queue_internal(message_queue->mqi);
    }
}


ZEND_BEGIN_ARG_INFO_EX(Threaded_run_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(Thread_add_task_arginfo, 0, 0, 1)
    ZEND_ARG_INFO(0, class_name)
ZEND_END_ARG_INFO()

PHP_METHOD(Thread, addTask)
{
    zend_class_entry *ce = Threaded_ce;
    zval *args;
    int argc = 0;

    ZEND_PARSE_PARAMETERS_START(1, -1)
        Z_PARAM_CLASS(ce)
        Z_PARAM_VARIADIC('*', args, argc)
    ZEND_PARSE_PARAMETERS_END();

    // By loading the class entry here, we ensure that it exists before
    // asynchronously creating the underlying threaded object. We can simply
    // discard the ce here and use only the ce name now

    thread_t *thread = (thread_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);
    task_t *task = malloc(sizeof(task_t));

    string_update(&task->class_name, ZSTR_VAL(ce->name), ZSTR_LEN(ce->name));
    task->class_ctor_argc = argc;

    if (argc) {
        task->class_ctor_args = malloc(sizeof(entry_t) * argc);

        for (int i = 0; i < argc; ++i) {
            pht_convert_zval_to_entry(task->class_ctor_args + i, args + i);
        }
    } else {
        task->class_ctor_args = NULL;
    }

    enqueue(&thread->tasks, task);
}

ZEND_BEGIN_ARG_INFO_EX(Thread_start_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Thread, start)
{
    thread_t *thread = (thread_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    main_thread.id = (ulong) pthread_self();
    main_thread.ls = TSRMLS_CACHE;

    pthread_create((pthread_t *)thread, NULL, (void *)worker_function, thread);
}

ZEND_BEGIN_ARG_INFO_EX(Thread_join_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Thread, join)
{
    thread_t *thread = (thread_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

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
    thread_t *thread = (thread_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    RETVAL_LONG(thread->tasks.size);
}

ZEND_BEGIN_ARG_INFO_EX(MessageQueue_push_arginfo, 0, 0, 1)
    ZEND_ARG_INFO(0, message)
ZEND_END_ARG_INFO()

PHP_METHOD(MessageQueue, push)
{
    message_queue_t *message_queue = (message_queue_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);
    zval *message;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ZVAL(message)
    ZEND_PARSE_PARAMETERS_END();

    message_queue_push(message_queue->mqi, create_new_message(create_new_entry(message)));
}

ZEND_BEGIN_ARG_INFO_EX(MessageQueue_pop_arginfo, 0, 0, 1)
    ZEND_ARG_INFO(1, message)
ZEND_END_ARG_INFO()

PHP_METHOD(MessageQueue, pop)
{
    message_queue_t *message_queue = (message_queue_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);
    zval *zmessage;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ZVAL_DEREF(zmessage)
    ZEND_PARSE_PARAMETERS_END();

    RETURN_BOOL(message_queue_pop(message_queue->mqi, zmessage));
}

ZEND_BEGIN_ARG_INFO_EX(MessageQueue_has_messages_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(MessageQueue, hasMessages)
{
    message_queue_t *message_queue = (message_queue_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    // @todo I don't think a mutex lock needs to be held for this?
    // I'm going to hold it anyway for now, and performance check things later
    pthread_mutex_lock(&message_queue->mqi->lock);
    RETVAL_BOOL(message_queue->mqi->messages != NULL);
    pthread_mutex_unlock(&message_queue->mqi->lock);
}

ZEND_BEGIN_ARG_INFO_EX(MessageQueue_set_state_arginfo, 0, 0, 1)
ZEND_END_ARG_INFO()

PHP_METHOD(MessageQueue, setState)
{
    message_queue_t *message_queue = (message_queue_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);
    zend_long state;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(state)
    ZEND_PARSE_PARAMETERS_END();

    // @todo I don't think a mutex lock needs to be held for this?
    // I'm going to hold it anyway for now, and performance check things later
    pthread_mutex_lock(&message_queue->mqi->lock);
    message_queue->mqi->state = state;
    pthread_mutex_unlock(&message_queue->mqi->lock);
}

ZEND_BEGIN_ARG_INFO_EX(MessageQueue_get_state_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(MessageQueue, getState)
{
    message_queue_t *message_queue = (message_queue_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    // @todo I don't think a mutex lock needs to be held for this?
    // I'm going to hold it anyway for now, and performance check things later
    // Also, this could probably be just a simple property instead of a method
    pthread_mutex_lock(&message_queue->mqi->lock);
    RETVAL_LONG(message_queue->mqi->state);
    pthread_mutex_unlock(&message_queue->mqi->lock);
}



zend_function_entry Threaded_methods[] = {
    PHP_ABSTRACT_ME(Threaded, run, Threaded_run_arginfo)
    PHP_FE_END
};

zend_function_entry Thread_methods[] = {
    PHP_ME(Thread, addTask, Thread_add_task_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(Thread, start, Thread_start_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(Thread, join, Thread_join_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(Thread, taskCount, Thread_task_count_arginfo, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

zend_function_entry MessageQueue_methods[] = {
    PHP_ME(MessageQueue, push, MessageQueue_push_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(MessageQueue, pop, MessageQueue_pop_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(MessageQueue, hasMessages, MessageQueue_has_messages_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(MessageQueue, setState, MessageQueue_set_state_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(MessageQueue, getState, MessageQueue_get_state_arginfo, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

zval *mqh_read_property_handle(zval *object, zval *member, int type, void **cache, zval *rv)
{
    zend_throw_exception(zend_ce_exception, "Properties on MessageQueue objects are not enabled", 0);

    return &EG(uninitialized_zval);
}

void mqh_write_property_handle(zval *object, zval *member, zval *value, void **cache_slot)
{
    zend_throw_exception(zend_ce_exception, "Properties on MessageQueue objects are not enabled", 0);
}

PHP_MINIT_FUNCTION(pht)
{
    zend_class_entry ce;
    zend_object_handlers *zh = zend_get_std_object_handlers();

    INIT_CLASS_ENTRY(ce, "Threaded", Threaded_methods);
    Threaded_ce = zend_register_internal_interface(&ce);

    INIT_CLASS_ENTRY(ce, "Thread", Thread_methods);
    Thread_ce = zend_register_internal_class(&ce);
    Thread_ce->create_object = thread_ctor;

    memcpy(&thread_handlers, zh, sizeof(zend_object_handlers));

    thread_handlers.offset = XtOffsetOf(thread_t, obj);
    thread_handlers.free_obj = th_free_obj;

    INIT_CLASS_ENTRY(ce, "MessageQueue", MessageQueue_methods);
    MessageQueue_ce = zend_register_internal_class(&ce);
    MessageQueue_ce->create_object = message_queue_ctor;

    memcpy(&message_queue_handlers, zh, sizeof(zend_object_handlers));

    message_queue_handlers.offset = XtOffsetOf(message_queue_t, obj);
    message_queue_handlers.free_obj = mqh_free_obj;
    message_queue_handlers.read_property = mqh_read_property_handle;
    message_queue_handlers.write_property = mqh_write_property_handle;

    threads.size = 16;
    threads.used = 0;
    threads.thread_table = malloc(threads.size * sizeof(thread_t));
    pthread_mutex_init(&threads.lock, NULL);

    return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(pht)
{
    pthread_mutex_destroy(&threads.lock);
    free(threads.thread_table);

    return SUCCESS;
}

PHP_RINIT_FUNCTION(pht)
{
    ZEND_TSRMLS_CACHE_UPDATE();

    zend_hash_init(&PHT_ZG(interned_strings), 8, NULL, ZVAL_PTR_DTOR, 0);
    PHT_ZG(skip_mqi_creation) = 0;
    // main_thread.id = (ulong) pthread_self();
    // main_thread.ls = TSRMLS_CACHE;

    return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(pht)
{
    zend_hash_destroy(&PHT_ZG(interned_strings));

    return SUCCESS;
}

PHP_MINFO_FUNCTION(pht)
{
    php_info_print_table_start();
    php_info_print_table_header(2, "pht support", "enabled");
    php_info_print_table_end();
}

zend_module_entry pht_module_entry = {
    STANDARD_MODULE_HEADER,
    "pht",
    NULL,
    PHP_MINIT(pht),
    PHP_MSHUTDOWN(pht),
    PHP_RINIT(pht),
    PHP_RSHUTDOWN(pht),
    PHP_MINFO(pht),
    PHP_PHT_VERSION,
    PHP_MODULE_GLOBALS(pht),
    NULL,
    NULL,
    NULL,
    STANDARD_MODULE_PROPERTIES_EX
};

#ifdef COMPILE_DL_PHT
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
#endif
ZEND_GET_MODULE(pht)
#endif

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
