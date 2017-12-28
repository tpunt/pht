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
#include "pht_debug.h"

zend_object_handlers thread_handlers;
zend_object_handlers queue_handlers;
zend_object_handlers hash_table_handlers;

zend_class_entry *Threaded_ce;
zend_class_entry *Thread_ce;
zend_class_entry *Queue_ce;
zend_class_entry *HashTable_ce;

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

void free_queue_internal(queue_obj_internal_t *qoi)
{
    pthread_mutex_destroy(&qoi->lock);

    while (qoi->entries.size) {
        // @todo check if object is either another MQ or a HT (its refcount will
        // need to be decremented if so).
        // This should go into a specific queue_destroy method.
        pht_entry_delete(dequeue(&qoi->entries));
    }

    free(qoi);
}

void free_hashtable_internal(hashtable_obj_internal_t *htoi)
{
    pthread_mutex_destroy(&htoi->lock);

    // How to see if any values are either another MQ or a HT (where their
    // refcounts will need to be decremented?). Doing so in the callback may not
    // be appropriate
    // What if a MQ is put into the HT, fetched from it, GCed, then the HT is
    // destroyed? The refcount will be decremented twice. Perhaps use pointer
    // tagging on entry_t value to mark the internal DS if it has been GCed?
    // What if it gets repopulated and then GCed again?
    pht_hashtable_destroy(&htoi->hashtable, pht_entry_delete);
    free(htoi);
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
        a HT or queue is passed in as an argument to Thread::addTask, then
        its reference count will be incremented. If no constructor is declared,
        however, then the constructor branch will never be hit, the
        corresponding HT or queue object will not be constructed, and so the
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

    // @todo clean up all undone tasks

    PG(report_memleaks) = 0;

    php_request_shutdown(NULL);

    ts_free_thread();

    pthread_exit(NULL);
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

static zend_object *queue_ctor(zend_class_entry *entry)
{
    queue_obj_t *qo = ecalloc(1, sizeof(queue_obj_t) + zend_object_properties_size(entry));

    zend_object_std_init(&qo->obj, entry);
    object_properties_init(&qo->obj, entry);

    qo->obj.handlers = &queue_handlers;
    qo->vn = 0;

    if (!PHT_ZG(skip_qoi_creation)) {
        queue_obj_internal_t *qoi = calloc(1, sizeof(queue_obj_internal_t));

        // qoi->state = 0;
        qoi->refcount = 1;
        qoi->vn = 0;
        pthread_mutex_init(&qoi->lock, NULL);

        qo->qoi = qoi;
    }

    return &qo->obj;
}

static zend_object *hash_table_ctor(zend_class_entry *entry)
{
    hashtable_obj_t *hto = ecalloc(1, sizeof(hashtable_obj_t) + zend_object_properties_size(entry));

    zend_object_std_init(&hto->obj, entry);
    object_properties_init(&hto->obj, entry);

    hto->obj.handlers = &hash_table_handlers;
    hto->vn = 0;

    if (!PHT_ZG(skip_htoi_creation)) {
        hashtable_obj_internal_t *htoi = calloc(1, sizeof(hashtable_obj_internal_t));

        pht_hashtable_init(&htoi->hashtable, 2);
        pthread_mutex_init(&htoi->lock, NULL);
        htoi->refcount = 1;
        htoi->vn = 0;

        hto->htoi = htoi;
    }

    return &hto->obj;
}

void qo_dtor_obj(zend_object *obj)
{
    zend_object_std_dtor(obj);
}

void qo_free_obj(zend_object *obj)
{
    queue_obj_t *qo = (queue_obj_t *)((char *)obj - obj->handlers->offset);

    pthread_mutex_lock(&qo->qoi->lock);
    --qo->qoi->refcount;
    pthread_mutex_unlock(&qo->qoi->lock);

    if (!qo->qoi->refcount) {
        free_queue_internal(qo->qoi);
    }
}

void hto_dtor_obj(zend_object *obj)
{
    zend_object_std_dtor(obj);
}

void hto_free_obj(zend_object *obj)
{
    hashtable_obj_t *hto = (hashtable_obj_t *)((char *)obj - obj->handlers->offset);

    pthread_mutex_lock(&hto->htoi->lock);
    --hto->htoi->refcount;
    pthread_mutex_unlock(&hto->htoi->lock);

    if (!hto->htoi->refcount) {
        free_hashtable_internal(hto->htoi);
    }
}

zval *hto_read_dimension(zval *zobj, zval *offset, int type, zval *rv)
{
    if (offset == NULL) {
        zend_throw_error(NULL, "Cannot read an empty offset");
        return NULL;
    }

    hashtable_obj_t *hto = (hashtable_obj_t *)((char *)Z_OBJ_P(zobj) - Z_OBJ_P(zobj)->handlers->offset);
    entry_t *e = NULL;

    switch (Z_TYPE_P(offset)) {
        case IS_STRING:
            {
                pht_string_t key;

                pht_str_update(&key, Z_STRVAL_P(offset), Z_STRLEN_P(offset));
                e = pht_hashtable_search(&hto->htoi->hashtable, &key);
                pht_str_free(&key);
            }
            break;
        case IS_LONG:
            e = pht_hashtable_search_ind(&hto->htoi->hashtable, Z_LVAL_P(offset));
            break;
        default:
            zend_throw_error(NULL, "Invalid offset type"); // @todo cater for Object::__toString()?
            return NULL;
    }

    if (!e) {
        if (type != BP_VAR_IS) {
            zend_throw_error(NULL, "Undefined offset");
        }
        return NULL; // correct?
    }

    pht_convert_entry_to_zval(rv, e);

    return rv;
}

void hto_write_dimension(zval *zobj, zval *offset, zval *value)
{
    hashtable_obj_t *hto = (hashtable_obj_t *)((char *)Z_OBJ_P(zobj) - Z_OBJ_P(zobj)->handlers->offset);
    entry_t *entry = create_new_entry(value);

    if (ENTRY_TYPE(entry) == PHT_SERIALISATION_FAILED) {
        zend_throw_error(NULL, "Failed to serialise the value");
        free(entry);
        return;
    }

    switch (Z_TYPE_P(offset)) {
        case IS_STRING:
            {
                pht_string_t *key = pht_str_new(Z_STRVAL_P(offset), Z_STRLEN_P(offset));

                if (pht_hashtable_search(&hto->htoi->hashtable, key)) {
                    pht_hashtable_update(&hto->htoi->hashtable, key, entry);
                } else {
                    pht_hashtable_insert(&hto->htoi->hashtable, key, entry);
                }

                ++hto->htoi->vn;
            }
            break;
        case IS_LONG:
            {
                if (pht_hashtable_search_ind(&hto->htoi->hashtable, Z_LVAL_P(offset))) {
                    pht_hashtable_update_ind(&hto->htoi->hashtable, Z_LVAL_P(offset), entry);
                } else {
                    pht_hashtable_insert_ind(&hto->htoi->hashtable, Z_LVAL_P(offset), entry);
                }

                ++hto->htoi->vn;
            }
            break;
        default:
            zend_throw_error(NULL, "Invalid offset type"); // @todo cater for Object::__toString()?
            return;
    }
}

HashTable *hto_get_debug_info(zval *zobj, int *is_temp)
{
    hashtable_obj_t *hto = (hashtable_obj_t *)((char *)Z_OBJ_P(zobj) - Z_OBJ_P(zobj)->handlers->offset);
    HashTable *zht = emalloc(sizeof(HashTable));

    zend_hash_init(zht, 8, NULL, ZVAL_PTR_DTOR, 0);
    *is_temp = 1;
    pht_hashtable_to_zend_hashtable(zht, &hto->htoi->hashtable);

    return zht;
}

int hto_count_elements(zval *zobj, zend_long *count)
{
    hashtable_obj_t *hto = (hashtable_obj_t *)((char *)Z_OBJ_P(zobj) - Z_OBJ_P(zobj)->handlers->offset);

    *count = hto->htoi->hashtable.used;

    return SUCCESS;
}

HashTable *hto_get_properties(zval *zobj)
{
    zend_object *obj = Z_OBJ_P(zobj);
    hashtable_obj_t *hto = (hashtable_obj_t *)((char *)obj - obj->handlers->offset);

    if (obj->properties && hto->vn == hto->htoi->vn) {
        return obj->properties;
    }

    HashTable *zht = emalloc(sizeof(HashTable));

    zend_hash_init(zht, 8, NULL, ZVAL_PTR_DTOR, 0);
    pht_hashtable_to_zend_hashtable(zht, &hto->htoi->hashtable);

    if (obj->properties) {
        // @todo safe? Perhaps just wipe HT and insert into it instead?
        GC_REFCOUNT(obj->properties) = 0;
        zend_array_destroy(obj->properties);
    }

    obj->properties = zht;
    hto->vn = hto->htoi->vn;

    return zht;
}

int hto_has_dimension(zval *zobj, zval *offset, int check_empty)
{
    zend_object *obj = Z_OBJ_P(zobj);
    hashtable_obj_t *hto = (hashtable_obj_t *)((char *)obj - obj->handlers->offset);
    entry_t *entry = NULL;

    switch (Z_TYPE_P(offset)) {
        case IS_STRING:
            {
                pht_string_t key;

                pht_str_update(&key, Z_STRVAL_P(offset), Z_STRLEN_P(offset));
                entry = pht_hashtable_search(&hto->htoi->hashtable, &key);
                pht_str_free(&key);
            }
            break;
        case IS_LONG:
            entry = pht_hashtable_search_ind(&hto->htoi->hashtable, Z_LVAL_P(offset));
            break;
        default:
            zend_throw_error(NULL, "Invalid offset type"); // @todo cater for Object::__toString()?
            return 0;
    }

    if (!entry) {
        return 0;
    }

    if (!check_empty) {
        return ENTRY_TYPE(entry) != IS_NULL;
    }

    zval value;
    int result;

    pht_convert_entry_to_zval(&value, entry);
    result = i_zend_is_true(&value);
    zval_ptr_dtor(&value);

    return result;
}

void hto_unset_dimension(zval *zobj, zval *offset)
{
    zend_object *obj = Z_OBJ_P(zobj);
    hashtable_obj_t *hto = (hashtable_obj_t *)((char *)obj - obj->handlers->offset);

    switch (Z_TYPE_P(offset)) {
        case IS_STRING:
            {
                pht_string_t key;

                pht_str_update(&key, Z_STRVAL_P(offset), Z_STRLEN_P(offset));
                pht_hashtable_delete(&hto->htoi->hashtable, &key, pht_entry_delete);
                pht_str_free(&key);
                ++hto->htoi->vn;
            }
            break;
        case IS_LONG:
            pht_hashtable_delete_ind(&hto->htoi->hashtable, Z_LVAL_P(offset), pht_entry_delete);
            ++hto->htoi->vn;
            break;
        default:
            zend_throw_error(NULL, "Invalid offset type"); // @todo cater for Object::__toString()?
    }
}

HashTable *qo_get_properties(zval *zobj)
{
    zend_object *obj = Z_OBJ_P(zobj);
    queue_obj_t *qo = (queue_obj_t *)((char *)obj - obj->handlers->offset);

    if (obj->properties && qo->vn == qo->qoi->vn) {
        return obj->properties;
    }

    HashTable *zht = emalloc(sizeof(HashTable));

    zend_hash_init(zht, queue_size(&qo->qoi->entries), NULL, ZVAL_PTR_DTOR, 0);
    pht_queue_to_zend_hashtable(zht, &qo->qoi->entries);

    if (obj->properties) {
        // @todo safe? Perhaps just wipe HT and insert into it instead?
        GC_REFCOUNT(obj->properties) = 0;
        zend_array_destroy(obj->properties);
    }

    obj->properties = zht;
    qo->vn = qo->qoi->vn;

    return zht;
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

    pht_str_update(&task->class_name, ZSTR_VAL(ce->name), ZSTR_LEN(ce->name));
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

ZEND_BEGIN_ARG_INFO_EX(Queue_push_arginfo, 0, 0, 1)
    ZEND_ARG_INFO(0, entry)
ZEND_END_ARG_INFO()

PHP_METHOD(Queue, push)
{
    queue_obj_t *qo = (queue_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);
    zval *entry;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ZVAL(entry)
    ZEND_PARSE_PARAMETERS_END();

    enqueue(&qo->qoi->entries, create_new_entry(entry));
}

ZEND_BEGIN_ARG_INFO_EX(Queue_pop_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Queue, pop)
{
    queue_obj_t *qo = (queue_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    entry_t *entry = dequeue(&qo->qoi->entries);
    pht_convert_entry_to_zval(return_value, entry);
    pht_entry_delete(entry);
}

// @todo what about count() function? Rebuilding prop table is not good...
ZEND_BEGIN_ARG_INFO_EX(Queue_size_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Queue, size)
{
    queue_obj_t *qo = (queue_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    RETVAL_LONG(queue_size(&qo->qoi->entries));
}

ZEND_BEGIN_ARG_INFO_EX(Queue_lock_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Queue, lock)
{
    queue_obj_t *qo = (queue_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    pthread_mutex_lock(&qo->qoi->lock);
}

ZEND_BEGIN_ARG_INFO_EX(Queue_unlock_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Queue, unlock)
{
    queue_obj_t *qo = (queue_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    pthread_mutex_unlock(&qo->qoi->lock);
}

// ZEND_BEGIN_ARG_INFO_EX(Queue_set_state_arginfo, 0, 0, 1)
// ZEND_END_ARG_INFO()
//
// PHP_METHOD(Queue, setState)
// {
//     queue_obj_t *qo = (queue_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);
//     zend_long state;
//
//     ZEND_PARSE_PARAMETERS_START(1, 1)
//         Z_PARAM_LONG(state)
//     ZEND_PARSE_PARAMETERS_END();
//
//     // @todo I don't think a mutex lock needs to be held for this?
//     // I'm going to hold it anyway for now, and performance check things later
//     pthread_mutex_lock(&qo->qoi->lock);
//     qo->qoi->state = state;
//     pthread_mutex_unlock(&qo->qoi->lock);
// }

// ZEND_BEGIN_ARG_INFO_EX(Queue_get_state_arginfo, 0, 0, 0)
// ZEND_END_ARG_INFO()
//
// PHP_METHOD(Queue, getState)
// {
//     queue_obj_t *qo = (queue_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);
//
//     if (zend_parse_parameters_none() != SUCCESS) {
//         return;
//     }
//
//     // @todo I don't think a mutex lock needs to be held for this?
//     // I'm going to hold it anyway for now, and performance check things later
//     // Also, this could probably be just a simple property instead of a method
//     pthread_mutex_lock(&qo->qoi->lock);
//     RETVAL_LONG(qo->qoi->state);
//     pthread_mutex_unlock(&qo->qoi->lock);
// }

ZEND_BEGIN_ARG_INFO_EX(HashTable_lock_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(HashTable, lock)
{
    hashtable_obj_t *hto = (hashtable_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    pthread_mutex_lock(&hto->htoi->lock);
}

ZEND_BEGIN_ARG_INFO_EX(HashTable_unlock_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(HashTable, unlock)
{
    hashtable_obj_t *hto = (hashtable_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    pthread_mutex_unlock(&hto->htoi->lock);
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

zend_function_entry Queue_methods[] = {
    PHP_ME(Queue, push, Queue_push_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(Queue, pop, Queue_pop_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(Queue, size, Queue_size_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(Queue, lock, Queue_lock_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(Queue, unlock, Queue_unlock_arginfo, ZEND_ACC_PUBLIC)
    // PHP_ME(Queue, setState, Queue_set_state_arginfo, ZEND_ACC_PUBLIC)
    // PHP_ME(Queue, getState, Queue_get_state_arginfo, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

zend_function_entry HashTable_methods[] = {
    PHP_ME(HashTable, lock, HashTable_lock_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(HashTable, unlock, HashTable_unlock_arginfo, ZEND_ACC_PUBLIC)
    // make countable
    PHP_FE_END
};

zval *qo_read_property(zval *object, zval *member, int type, void **cache, zval *rv)
{
    zend_throw_exception(zend_ce_exception, "Properties on Queue objects are not enabled", 0);

    return &EG(uninitialized_zval);
}

void qo_write_property(zval *object, zval *member, zval *value, void **cache_slot)
{
    zend_throw_exception(zend_ce_exception, "Properties on Queue objects are not enabled", 0);
}

zval *hto_read_property(zval *object, zval *member, int type, void **cache, zval *rv)
{
    zend_throw_exception(zend_ce_exception, "Properties on HashTable objects are not enabled", 0);

    return &EG(uninitialized_zval);
}

void hto_write_property(zval *object, zval *member, zval *value, void **cache_slot)
{
    zend_throw_exception(zend_ce_exception, "Properties on HashTable objects are not enabled", 0);
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

    INIT_CLASS_ENTRY(ce, "Queue", Queue_methods);
    Queue_ce = zend_register_internal_class(&ce);
    Queue_ce->create_object = queue_ctor;

    memcpy(&queue_handlers, zh, sizeof(zend_object_handlers));

    queue_handlers.offset = XtOffsetOf(queue_obj_t, obj);
    queue_handlers.dtor_obj = qo_dtor_obj;
    queue_handlers.free_obj = qo_free_obj;
    queue_handlers.read_property = qo_read_property;
    queue_handlers.write_property = qo_write_property;
    queue_handlers.get_properties = qo_get_properties;

    INIT_CLASS_ENTRY(ce, "HashTable", HashTable_methods);
    HashTable_ce = zend_register_internal_class(&ce);
    HashTable_ce->create_object = hash_table_ctor;

    memcpy(&hash_table_handlers, zh, sizeof(zend_object_handlers));

    hash_table_handlers.offset = XtOffsetOf(hashtable_obj_t, obj);
    hash_table_handlers.dtor_obj = hto_dtor_obj;
    hash_table_handlers.free_obj = hto_free_obj;
    hash_table_handlers.read_property = hto_read_property;
    hash_table_handlers.write_property = hto_write_property;
    hash_table_handlers.read_dimension = hto_read_dimension;
    hash_table_handlers.write_dimension = hto_write_dimension;
    hash_table_handlers.get_debug_info = hto_get_debug_info;
    hash_table_handlers.count_elements = hto_count_elements;
    hash_table_handlers.get_properties = hto_get_properties;
    hash_table_handlers.has_dimension = hto_has_dimension;
    hash_table_handlers.unset_dimension = hto_unset_dimension;

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
    PHT_ZG(skip_qoi_creation) = 0;
    PHT_ZG(skip_htoi_creation) = 0;
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
