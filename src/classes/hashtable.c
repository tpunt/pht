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
#include "src/classes/hashtable.h"

extern zend_class_entry *Threaded_ce;

zend_object_handlers hash_table_handlers;
zend_class_entry *HashTable_ce;

void htoi_free(hashtable_obj_internal_t *htoi)
{
    pthread_mutex_destroy(&htoi->lock);
    pht_hashtable_destroy(&htoi->hashtable);
    free(htoi);
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
        pthread_mutexattr_t attr;

        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
        pthread_mutex_init(&htoi->lock, &attr);
        pthread_mutexattr_destroy(&attr);

        pht_hashtable_init(&htoi->hashtable, 2, pht_entry_delete);
        htoi->refcount = 1;
        htoi->vn = 0;

        hto->htoi = htoi;
    }

    return &hto->obj;
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
        htoi_free(hto->htoi);
    }
}

zval *hto_read_dimension(zval *zobj, zval *offset, int type, zval *rv)
{
    if (offset == NULL) {
        zend_throw_error(NULL, "Cannot read an empty offset");
        return NULL;
    }

    hashtable_obj_t *hto = (hashtable_obj_t *)((char *)Z_OBJ_P(zobj) - Z_OBJ_P(zobj)->handlers->offset);
    pht_entry_t *e = NULL;

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

    if (!offset) {
        zend_throw_error(NULL, "Empty offset insertions are not allowed");
        return;
    }

    pht_entry_t *entry = pht_create_entry_from_zval(value);

    if (!entry) {
        zend_throw_error(NULL, "Failed to serialise the value");
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

int hto_has_dimension(zval *zobj, zval *offset, int check_empty)
{
    zend_object *obj = Z_OBJ_P(zobj);
    hashtable_obj_t *hto = (hashtable_obj_t *)((char *)obj - obj->handlers->offset);
    pht_entry_t *entry = NULL;

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
        return PHT_ENTRY_TYPE(entry) != IS_NULL;
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
                pht_hashtable_delete(&hto->htoi->hashtable, &key);
                pht_str_free(&key);
                ++hto->htoi->vn;
            }
            break;
        case IS_LONG:
            pht_hashtable_delete_ind(&hto->htoi->hashtable, Z_LVAL_P(offset));
            ++hto->htoi->vn;
            break;
        default:
            zend_throw_error(NULL, "Invalid offset type"); // @todo cater for Object::__toString()?
    }
}

HashTable *hto_get_properties(zval *zobj)
{
    zend_object *obj = Z_OBJ_P(zobj);
    hashtable_obj_t *hto = (hashtable_obj_t *)((char *)obj - obj->handlers->offset);

    if (obj->properties && hto->vn == hto->htoi->vn) {
        return obj->properties;
    }

    if (obj->properties) {
        zend_hash_clean(obj->properties);
    } else {
        obj->properties = emalloc(sizeof(HashTable));
        zend_hash_init(obj->properties, 8, NULL, ZVAL_PTR_DTOR, 0);
    }

    pht_hashtable_to_zend_hashtable(obj->properties, &hto->htoi->hashtable);

    hto->vn = hto->htoi->vn;

    return obj->properties;
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

ZEND_BEGIN_ARG_INFO_EX(HashTable_lock_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(HashTable, lock)
{
    hashtable_obj_t *hto = (hashtable_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    if (pthread_mutex_lock(&hto->htoi->lock)) {
        zend_throw_error(NULL, "This mutex lock is already being held by this thread");
    }
}

ZEND_BEGIN_ARG_INFO_EX(HashTable_unlock_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(HashTable, unlock)
{
    hashtable_obj_t *hto = (hashtable_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    if (pthread_mutex_unlock(&hto->htoi->lock)) {
        zend_throw_error(NULL, "This mutex lock is either unheld, or is currently being held by another thread");
    }
}

ZEND_BEGIN_ARG_INFO_EX(HashTable_size_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(HashTable, size)
{
    hashtable_obj_t *hto = (hashtable_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    RETVAL_LONG(hto->htoi->hashtable.used);
}

zend_function_entry HashTable_methods[] = {
    PHP_ME(HashTable, lock, HashTable_lock_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(HashTable, unlock, HashTable_unlock_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(HashTable, size, HashTable_size_arginfo, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

zval *hto_read_property(zval *object, zval *member, int type, void **cache, zval *rv)
{
    zend_throw_error(zend_ce_error, "Properties on HashTable objects are not enabled", 0);

    return &EG(uninitialized_zval);
}

void hto_write_property(zval *object, zval *member, zval *value, void **cache_slot)
{
    zend_throw_error(zend_ce_error, "Properties on HashTable objects are not enabled", 0);
}

void hashtable_ce_init(void)
{
    zend_class_entry ce;
    zend_object_handlers *zh = zend_get_std_object_handlers();

    INIT_CLASS_ENTRY(ce, "pht\\HashTable", HashTable_methods);
    HashTable_ce = zend_register_internal_class(&ce);
    HashTable_ce->create_object = hash_table_ctor;
    HashTable_ce->ce_flags |= ZEND_ACC_FINAL;
    HashTable_ce->serialize = zend_class_serialize_deny;
    HashTable_ce->unserialize = zend_class_unserialize_deny;

    zend_class_implements(HashTable_ce, 1, Threaded_ce);
    memcpy(&hash_table_handlers, zh, sizeof(zend_object_handlers));

    hash_table_handlers.offset = XtOffsetOf(hashtable_obj_t, obj);
    hash_table_handlers.dtor_obj = hto_dtor_obj;
    hash_table_handlers.free_obj = hto_free_obj;
    hash_table_handlers.read_property = hto_read_property;
    hash_table_handlers.write_property = hto_write_property;
    hash_table_handlers.read_dimension = hto_read_dimension;
    hash_table_handlers.write_dimension = hto_write_dimension;
    hash_table_handlers.get_debug_info = hto_get_debug_info;
    hash_table_handlers.get_properties = hto_get_properties;
    hash_table_handlers.has_dimension = hto_has_dimension;
    hash_table_handlers.unset_dimension = hto_unset_dimension;
}
