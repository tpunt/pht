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

#include <Zend/zend_API.h>
#include <Zend/zend_exceptions.h>
#include <Zend/zend_interfaces.h>

#include "php_pht.h"
#include "src/pht_entry.h"
#include "src/pht_debug.h"
#include "src/classes/queue.h"

extern zend_class_entry *Threaded_ce;

zend_object_handlers queue_handlers;
zend_class_entry *Queue_ce;

void qoi_free(queue_obj_internal_t *qoi)
{
    pthread_mutex_destroy(&qoi->lock);
    pht_queue_destroy(&qoi->queue);
    free(qoi);
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
        pthread_mutexattr_t attr;

        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
        pthread_mutex_init(&qoi->lock, &attr);
        pthread_mutexattr_destroy(&attr);

        pht_queue_init(&qoi->queue, pht_entry_delete);
        qoi->refcount = 1;
        qoi->vn = 0;

        qo->qoi = qoi;
    }

    return &qo->obj;
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
        qoi_free(qo->qoi);
    }
}

HashTable *qo_get_properties(zval *zobj)
{
    zend_object *obj = Z_OBJ_P(zobj);
    queue_obj_t *qo = (queue_obj_t *)((char *)obj - obj->handlers->offset);

    if (obj->properties && qo->vn == qo->qoi->vn) {
        return obj->properties;
    }

    if (obj->properties) {
        zend_hash_clean(obj->properties);
    } else {
        obj->properties = emalloc(sizeof(HashTable));
        zend_hash_init(obj->properties, pht_queue_size(&qo->qoi->queue), NULL, ZVAL_PTR_DTOR, 0);
    }

    pht_queue_to_zend_hashtable(obj->properties, &qo->qoi->queue);

    qo->vn = qo->qoi->vn;

    return obj->properties;
}

ZEND_BEGIN_ARG_INFO_EX(Queue_push_arginfo, 0, 0, 1)
    ZEND_ARG_INFO(0, entry)
ZEND_END_ARG_INFO()

PHP_METHOD(Queue, push)
{
    queue_obj_t *qo = (queue_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);
    zval *value;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ZVAL(value)
    ZEND_PARSE_PARAMETERS_END();

    pht_entry_t *entry = pht_create_entry_from_zval(value);

    if (!entry) {
        zend_throw_error(NULL, "Failed to serialise the value");
        return;
    }

    pht_queue_push(&qo->qoi->queue, entry);
    ++qo->qoi->vn;
}

ZEND_BEGIN_ARG_INFO_EX(Queue_pop_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Queue, pop)
{
    queue_obj_t *qo = (queue_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    pht_entry_t *entry = pht_queue_pop(&qo->qoi->queue);

    if (!entry) {
        zend_throw_error(NULL, "Attempted to pop an element from an empty queue");
        return;
    }

    pht_convert_entry_to_zval(return_value, entry);
    pht_entry_delete(entry);
    ++qo->qoi->vn;
}

ZEND_BEGIN_ARG_INFO_EX(Queue_front_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Queue, front)
{
    queue_obj_t *qo = (queue_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    pht_entry_t *entry = pht_queue_front(&qo->qoi->queue);

    if (!entry) {
        zend_throw_error(NULL, "Attempted to fetch an element from an empty queue");
        return;
    }

    pht_convert_entry_to_zval(return_value, entry);
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

    RETVAL_LONG(pht_queue_size(&qo->qoi->queue));
}

ZEND_BEGIN_ARG_INFO_EX(Queue_lock_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Queue, lock)
{
    queue_obj_t *qo = (queue_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    if (pthread_mutex_lock(&qo->qoi->lock)) {
        zend_throw_error(NULL, "This mutex lock is already being held by this thread");
    }
}

ZEND_BEGIN_ARG_INFO_EX(Queue_unlock_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Queue, unlock)
{
    queue_obj_t *qo = (queue_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    if (pthread_mutex_unlock(&qo->qoi->lock)) {
        zend_throw_error(NULL, "This mutex lock is either unheld, or is currently being held by another thread");
    }
}

zend_function_entry Queue_methods[] = {
    PHP_ME(Queue, push, Queue_push_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(Queue, pop, Queue_pop_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(Queue, front, Queue_front_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(Queue, size, Queue_size_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(Queue, lock, Queue_lock_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(Queue, unlock, Queue_unlock_arginfo, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

zval *qo_read_property(zval *object, zval *member, int type, void **cache, zval *rv)
{
    zend_throw_error(zend_ce_error, "Properties on Queue objects are not enabled", 0);

    return &EG(uninitialized_zval);
}

void qo_write_property(zval *object, zval *member, zval *value, void **cache_slot)
{
    zend_throw_error(zend_ce_error, "Properties on Queue objects are not enabled", 0);
}

void queue_ce_init(void)
{
    zend_class_entry ce;
    zend_object_handlers *zh = zend_get_std_object_handlers();

    INIT_CLASS_ENTRY(ce, "pht\\Queue", Queue_methods);
    Queue_ce = zend_register_internal_class(&ce);
    Queue_ce->create_object = queue_ctor;
    Queue_ce->ce_flags |= ZEND_ACC_FINAL;
    Queue_ce->serialize = zend_class_serialize_deny;
    Queue_ce->unserialize = zend_class_unserialize_deny;

    zend_class_implements(Queue_ce, 1, Threaded_ce);
    memcpy(&queue_handlers, zh, sizeof(zend_object_handlers));

    queue_handlers.offset = XtOffsetOf(queue_obj_t, obj);
    queue_handlers.dtor_obj = qo_dtor_obj;
    queue_handlers.free_obj = qo_free_obj;
    queue_handlers.read_property = qo_read_property;
    queue_handlers.write_property = qo_write_property;
    queue_handlers.get_properties = qo_get_properties;
}
