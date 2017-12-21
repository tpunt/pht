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

zend_object_handlers threadref_handlers;
zend_object_handlers message_queue_handlers;

zend_class_entry *Threaded_ce;
zend_class_entry *ThreadRef_ce;
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
    pthread_mutex_init(&thread->lock, NULL);
}

void thread_destroy(thread_t *thread)
{
    pthread_mutex_destroy(&thread->lock);
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

    zend_string *ce_name = zend_string_init(thread->class_name.val, thread->class_name.len, 0);
    zend_class_entry *ce = zend_fetch_class_by_name(ce_name, NULL, ZEND_FETCH_CLASS_DEFAULT | ZEND_FETCH_CLASS_EXCEPTION);
    zend_function *constructor, *run;
    zval zobj;

    if (object_init_ex(&zobj, ce) != SUCCESS) {
        // @todo this will throw an exception in the new thread, rather than at
        // the call site. This doesn't even have an execution context - how
        // should it behave?
        zend_throw_exception_ex(zend_ce_exception, 0, "Failed to threaded object from class '%s'\n", ZSTR_VAL(ce_name));
        goto finish;
    }

    // inc Z_OBJ(zobj) refcount ?

    thread->threaded_object = Z_OBJ(zobj);

    constructor = Z_OBJ_HT(zobj)->get_constructor(Z_OBJ(zobj));

    if (constructor) {
        int result;
        zval retval, zargs[thread->class_ctor_argc];
        zend_fcall_info fci;

        for (int i = 0; i < thread->class_ctor_argc; ++i) {
            pht_convert_entry_to_zval(zargs + i, thread->class_ctor_args + i);
        }

        fci.size = sizeof(fci);
        fci.object = Z_OBJ(zobj);
        fci.retval = &retval;
        fci.param_count = thread->class_ctor_argc;
        fci.params = zargs;
        fci.no_separation = 1;
        ZVAL_STRINGL(&fci.function_name, "__construct", sizeof("__construct") - 1);

        result = zend_call_function(&fci, NULL);

        if (result == FAILURE) {
            if (!EG(exception)) {
                // same as problem above?
                zend_error_noreturn(E_CORE_ERROR, "Couldn't execute method %s%s%s", ZSTR_VAL(ce_name), "::", "__construct");
                goto finish;
            }
        }

        zval_dtor(&fci.function_name);
        // dtor on retval?
    }

    pthread_mutex_lock(&thread->lock);
    if (thread->status == UNDER_CONSTRUCTION) {
        thread->status = ACTIVE;
    }
    pthread_mutex_unlock(&thread->lock);

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
            goto finish;
        }
    }

    zval_dtor(&fci.function_name);

finish:
    zend_string_free(ce_name);

    // @todo pause here until ThreadRef::join is invoked

    PG(report_memleaks) = 0;

    php_request_shutdown(NULL);

    ts_free_thread();

    pthread_exit(NULL);
}

void message_queue_push(message_queue_t *message_queue, message_t *message)
{
    pthread_mutex_lock(&message_queue->lock);

    if (!message_queue->messages) {
        message_queue->messages = message;
    } else {
        message_queue->last_message->next = message;
    }

    message_queue->last_message = message;

    pthread_mutex_unlock(&message_queue->lock);
}

zend_bool message_queue_pop(message_queue_t *message_queue, zval *zmessage)
{
    message_t *emessage = NULL;

    pthread_mutex_lock(&message_queue->lock);

    if (message_queue->messages) {
        emessage = message_queue->messages;
        message_queue->messages = message_queue->messages->next;

        if (!message_queue->messages) {
            message_queue->last_message = NULL;
        }
    }

    pthread_mutex_unlock(&message_queue->lock);

    if (!emessage) {
        return 0;
    }

    pht_convert_entry_to_zval(zmessage, emessage->message);
    free(emessage->message);
    free(emessage);

    return 1;
}

static zend_object *threadref_ctor(zend_class_entry *entry)
{
    thread_t *thread = ecalloc(1, sizeof(thread_t) + zend_object_properties_size(entry));
    int tid = aquire_thread_id();

    thread_init(thread, tid);

    zend_object_std_init(&thread->obj, entry);
    object_properties_init(&thread->obj, entry);

    thread->obj.handlers = &threadref_handlers;

    pthread_mutex_lock(&threads.lock);
    threads.thread_table[tid] = thread;
    pthread_mutex_unlock(&threads.lock);

    return &thread->obj;
}

static zend_object *message_queue_ctor(zend_class_entry *entry)
{
    message_queue_t *message_queue = ecalloc(1, sizeof(message_queue_t) + zend_object_properties_size(entry));

    zend_object_std_init(&message_queue->obj, entry);
    object_properties_init(&message_queue->obj, entry);

    message_queue->obj.handlers = &message_queue_handlers;

    message_queue->messages = NULL;
    message_queue->status = ACTIVE;
    pthread_mutex_init(&message_queue->lock, NULL);

    return &message_queue->obj;
}


ZEND_BEGIN_ARG_INFO_EX(ThreadRef___construct_arginfo, 0, 0, 1)
    ZEND_ARG_INFO(0, class_name)
ZEND_END_ARG_INFO()

PHP_METHOD(ThreadRef, __construct)
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

    string_update(&thread->class_name, ZSTR_VAL(ce->name), ZSTR_LEN(ce->name));
    thread->class_ctor_argc = argc;

    if (argc) {
        thread->class_ctor_args = malloc(sizeof(entry_t) * argc);

        for (int i = 0; i < argc; ++i) {
            pht_convert_zval_to_entry(thread->class_ctor_args + i, args + i);
        }
    } else {
        thread->class_ctor_args = NULL;
    }
}

ZEND_BEGIN_ARG_INFO_EX(ThreadRef_start_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(ThreadRef, start)
{
    thread_t *thread = (thread_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    main_thread.id = (ulong) pthread_self();
    main_thread.ls = TSRMLS_CACHE;

    pthread_create((pthread_t *)thread, NULL, (void *)worker_function, thread);
}

ZEND_BEGIN_ARG_INFO_EX(ThreadRef_join_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(ThreadRef, join)
{
    thread_t *thread = (thread_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    pthread_join(thread->thread, NULL);
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

    message_queue_push(message_queue, create_new_message(create_new_entry(message)));
}

ZEND_BEGIN_ARG_INFO_EX(MessageQueue_pop_arginfo, 0, 0, 1)
    ZEND_ARG_INFO(0, message)
ZEND_END_ARG_INFO()

PHP_METHOD(MessageQueue, pop)
{
    message_queue_t *message_queue = (message_queue_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);
    zval *zmessage;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ZVAL(zmessage)
    ZEND_PARSE_PARAMETERS_END();

    RETURN_BOOL(message_queue_pop(message_queue, zmessage));
}

ZEND_BEGIN_ARG_INFO_EX(MessageQueue_finish_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(MessageQueue, finish)
{
    message_queue_t *message_queue = (message_queue_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    // @todo I don't think a mutex lock needs to be held for this?
    // I'm going to hold it anyway for now, and performance check things later
    pthread_mutex_lock(&message_queue->lock);
    message_queue->status = FINISHED;
    pthread_mutex_unlock(&message_queue->lock);
}

ZEND_BEGIN_ARG_INFO_EX(MessageQueue_is_finished_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(MessageQueue, isFinished)
{
    message_queue_t *message_queue = (message_queue_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    // @todo I don't think a mutex lock needs to be held for this?
    // I'm going to hold it anyway for now, and performance check things later
    // Also, this could probably be just a simple property instead of a method
    pthread_mutex_lock(&message_queue->lock);
    zend_bool finished = message_queue->status == FINISHED;
    pthread_mutex_unlock(&message_queue->lock);

    RETURN_BOOL(finished);
}



zend_function_entry Threaded_methods[] = {
    PHP_FE_END
};

zend_function_entry ThreadRef_methods[] = {
    PHP_ME(ThreadRef, __construct, ThreadRef___construct_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(ThreadRef, start, ThreadRef_start_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(ThreadRef, join, ThreadRef_join_arginfo, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

zend_function_entry MessageQueue_methods[] = {
    PHP_ME(MessageQueue, push, MessageQueue_push_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(MessageQueue, pop, MessageQueue_pop_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(MessageQueue, finish, MessageQueue_finish_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(MessageQueue, isFinished, MessageQueue_is_finished_arginfo, ZEND_ACC_PUBLIC)
    PHP_FE_END
};



PHP_MINIT_FUNCTION(pht)
{
    zend_class_entry ce;
    zend_object_handlers *zh = zend_get_std_object_handlers();

    INIT_CLASS_ENTRY(ce, "Threaded", Threaded_methods);
	Threaded_ce = zend_register_internal_interface(&ce);

    // @todo should be a final class for safety (due to __construct usage)
    INIT_CLASS_ENTRY(ce, "ThreadRef", ThreadRef_methods);
    ThreadRef_ce = zend_register_internal_class(&ce);
    ThreadRef_ce->create_object = threadref_ctor;

    memcpy(&threadref_handlers, zh, sizeof(zend_object_handlers));

    threadref_handlers.offset = XtOffsetOf(thread_t, obj);

    INIT_CLASS_ENTRY(ce, "MessageQueue", MessageQueue_methods);
    MessageQueue_ce = zend_register_internal_class(&ce);
    MessageQueue_ce->create_object = message_queue_ctor;

    message_queue_handlers.offset = XtOffsetOf(message_queue_t, obj);

    threads.size = 16;
    threads.used = 0;
    threads.thread_table = malloc(threads.size * sizeof(thread_t));
    pthread_mutex_init(&threads.lock, NULL);

	return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(pht)
{
    pthread_mutex_destroy(&threads.lock);

	return SUCCESS;
}

PHP_RINIT_FUNCTION(pht)
{
	ZEND_TSRMLS_CACHE_UPDATE();

    zend_hash_init(&PHT_ZG(interned_strings), 8, NULL, ZVAL_PTR_DTOR, 0);
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
	PHP_RINIT(pht),		/* Replace with NULL if there's nothing to do at request start */
	PHP_RSHUTDOWN(pht),	/* Replace with NULL if there's nothing to do at request end */
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
