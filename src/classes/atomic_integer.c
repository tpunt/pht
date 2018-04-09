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
#include <Zend/zend_interfaces.h>

#include "php_pht.h"
#include "src/pht_debug.h"
#include "src/classes/atomic_integer.h"

extern zend_class_entry *Threaded_ce;

zend_object_handlers atomic_integer_handlers;
zend_class_entry *AtomicInteger_ce;

void aioi_free(atomic_integer_obj_internal_t *aioi)
{
    pthread_mutex_destroy(&aioi->lock);
    free(aioi);
}

static zend_object *atomic_integer_ctor(zend_class_entry *entry)
{
    atomic_integer_obj_t *aio = ecalloc(1, sizeof(atomic_integer_obj_t) + zend_object_properties_size(entry));

    zend_object_std_init(&aio->obj, entry);
    object_properties_init(&aio->obj, entry);

    aio->obj.handlers = &atomic_integer_handlers;

    if (!PHT_ZG(skip_aioi_creation)) {
        atomic_integer_obj_internal_t *aioi = calloc(1, sizeof(atomic_integer_obj_internal_t));
        pthread_mutexattr_t attr;

        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

        aioi->value = 0;
        pthread_mutex_init(&aioi->lock, &attr);
        aioi->refcount = 1;

        aio->aioi = aioi;
    }

    return &aio->obj;
}

void aio_dtor_obj(zend_object *obj)
{
    zend_object_std_dtor(obj);
}

void aio_free_obj(zend_object *obj)
{
    atomic_integer_obj_t *aio = (atomic_integer_obj_t *)((char *)obj - obj->handlers->offset);

    pthread_mutex_lock(&aio->aioi->lock);
    --aio->aioi->refcount;
    pthread_mutex_unlock(&aio->aioi->lock);

    if (!aio->aioi->refcount) {
        aioi_free(aio->aioi);
    }
}

HashTable *aio_get_properties(zval *zobj)
{
    zend_object *obj = Z_OBJ_P(zobj);
    atomic_integer_obj_t *aio = (atomic_integer_obj_t *)((char *)obj - obj->handlers->offset);
    zval value;

    pthread_mutex_lock(&aio->aioi->lock);
    ZVAL_LONG(&value, aio->aioi->value);
    pthread_mutex_unlock(&aio->aioi->lock);

    if (obj->properties) {
        zend_hash_update(obj->properties, common_strings.value, &value);
    } else {
        ALLOC_HASHTABLE(obj->properties);
        zend_hash_init(obj->properties, 1, NULL, ZVAL_PTR_DTOR, 0);
        zend_hash_add(obj->properties, common_strings.value, &value);
    }

    return obj->properties;
}

ZEND_BEGIN_ARG_INFO_EX(AtomicInteger___construct_arginfo, 0, 0, 0)
    ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

PHP_METHOD(AtomicInteger, __construct)
{
    atomic_integer_obj_t *aio = (atomic_integer_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);
    zend_long value = 0;

    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(value)
    ZEND_PARSE_PARAMETERS_END();

    aio->aioi->value = value;
}

ZEND_BEGIN_ARG_INFO_EX(AtomicInteger_get_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(AtomicInteger, get)
{
    atomic_integer_obj_t *aio = (atomic_integer_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    RETVAL_LONG(aio->aioi->value);
}

ZEND_BEGIN_ARG_INFO_EX(AtomicInteger_set_arginfo, 0, 0, 1)
    ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

PHP_METHOD(AtomicInteger, set)
{
    atomic_integer_obj_t *aio = (atomic_integer_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);
    zend_long value;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(value)
    ZEND_PARSE_PARAMETERS_END();

    aio->aioi->value = value;
}

ZEND_BEGIN_ARG_INFO_EX(AtomicInteger_inc_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(AtomicInteger, inc)
{
    atomic_integer_obj_t *aio = (atomic_integer_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    pthread_mutex_lock(&aio->aioi->lock);
    ++aio->aioi->value;
    pthread_mutex_unlock(&aio->aioi->lock);
}

ZEND_BEGIN_ARG_INFO_EX(AtomicInteger_dec_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(AtomicInteger, dec)
{
    atomic_integer_obj_t *aio = (atomic_integer_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    pthread_mutex_lock(&aio->aioi->lock);
    --aio->aioi->value;
    pthread_mutex_unlock(&aio->aioi->lock);
}

ZEND_BEGIN_ARG_INFO_EX(AtomicInteger_lock_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(AtomicInteger, lock)
{
    atomic_integer_obj_t *aio = (atomic_integer_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    pthread_mutex_lock(&aio->aioi->lock);
}

ZEND_BEGIN_ARG_INFO_EX(AtomicInteger_unlock_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(AtomicInteger, unlock)
{
    atomic_integer_obj_t *aio = (atomic_integer_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    pthread_mutex_unlock(&aio->aioi->lock);
}

zend_function_entry AtomicInteger_methods[] = {
    PHP_ME(AtomicInteger, __construct, AtomicInteger___construct_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(AtomicInteger, get, AtomicInteger_get_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(AtomicInteger, set, AtomicInteger_set_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(AtomicInteger, inc, AtomicInteger_inc_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(AtomicInteger, dec, AtomicInteger_dec_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(AtomicInteger, lock, AtomicInteger_lock_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(AtomicInteger, unlock, AtomicInteger_unlock_arginfo, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

void atomic_integer_ce_init(void)
{
    zend_class_entry ce;
    zend_object_handlers *zh = zend_get_std_object_handlers();

    INIT_CLASS_ENTRY(ce, "pht\\AtomicInteger", AtomicInteger_methods);
    AtomicInteger_ce = zend_register_internal_class(&ce);
    AtomicInteger_ce->create_object = atomic_integer_ctor;
    AtomicInteger_ce->ce_flags |= ZEND_ACC_FINAL;
    AtomicInteger_ce->serialize = zend_class_serialize_deny;
    AtomicInteger_ce->unserialize = zend_class_unserialize_deny;

    zend_class_implements(AtomicInteger_ce, 1, Threaded_ce);
    memcpy(&atomic_integer_handlers, zh, sizeof(zend_object_handlers));

    atomic_integer_handlers.offset = XtOffsetOf(atomic_integer_obj_t, obj);
    atomic_integer_handlers.dtor_obj = aio_dtor_obj;
    atomic_integer_handlers.free_obj = aio_free_obj;
    atomic_integer_handlers.get_properties = aio_get_properties;
}
