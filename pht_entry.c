/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2016 The PHP Group                                |
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php_pht.h"
#include "pht_copy.h"

void pht_entry_delete(void *entry_void)
{
    entry_t *entry = entry_void;

    pht_entry_delete_value(entry);

    free(entry);
}

void pht_entry_delete_value(entry_t *entry)
{
    switch (ENTRY_TYPE(entry)) {
        case PHT_STORE_FUNC:
            free(ENTRY_FUNC(entry));
            break;
        case IS_ARRAY:
        case IS_STRING:
            free(PHT_STRV(ENTRY_STRING(entry)));
    }
}

void pht_convert_entry_to_zval(zval *value, entry_t *e)
{
    switch (ENTRY_TYPE(e)) {
        case IS_STRING:
            ZVAL_STR(value, zend_string_init(PHT_STRV(ENTRY_STRING(e)), PHT_STRL(ENTRY_STRING(e)), 0));
            break;
        case IS_LONG:
            ZVAL_LONG(value, ENTRY_LONG(e));
            break;
        case IS_DOUBLE:
            ZVAL_DOUBLE(value, ENTRY_DOUBLE(e));
            break;
        case _IS_BOOL:
            ZVAL_BOOL(value, ENTRY_BOOL(e));
            break;
        case IS_NULL:
            ZVAL_NULL(value);
            break;
        case IS_ARRAY:
            {
                size_t buf_len = PHT_STRL(ENTRY_STRING(e));
                const unsigned char *p = (const unsigned char *) PHT_STRV(ENTRY_STRING(e));
                php_unserialize_data_t var_hash;

                PHP_VAR_UNSERIALIZE_INIT(var_hash);

                if (!php_var_unserialize(value, &p, p + buf_len, &var_hash)) {
                    // @todo handle serialisation failure - is this even possible to hit?
                }

                PHP_VAR_UNSERIALIZE_DESTROY(var_hash);
            }
            break;
        case PHT_STORE_FUNC:
            {
                zend_function *closure = copy_user_function(ENTRY_FUNC(e), NULL);
                char *name;
                size_t name_len;

                zend_create_closure(value, closure, zend_get_executed_scope(), closure->common.scope, NULL);
                name_len = spprintf(&name, 0, "Closure@%p", zend_get_closure_method_def(value));

                if (!zend_hash_str_update_ptr(EG(function_table), name, name_len, closure)) {
                    printf("FAILED!\n");
                }

                efree(name);
            }
            break;
        case PHT_QUEUE:
            {
                zend_string *ce_name = zend_string_init("Queue", sizeof("Queue") - 1, 0);
                zend_class_entry *ce = zend_fetch_class_by_name(ce_name, NULL, ZEND_FETCH_CLASS_DEFAULT | ZEND_FETCH_CLASS_EXCEPTION);
                zval zobj;

                PHT_ZG(skip_qoi_creation) = 1;

                if (object_init_ex(&zobj, ce) != SUCCESS) {
                    // @todo this will throw an exception in the new thread, rather than at
                    // the call site - how should it behave?
                    zend_throw_exception(zend_ce_exception, "Failed to threaded object from Queue class", 0);
                }

                PHT_ZG(skip_qoi_creation) = 0;

                queue_obj_t *old_qo = (queue_obj_t *)((char *)&ENTRY_Q(e)->obj - ENTRY_Q(e)->obj.handlers->offset);
                queue_obj_t *new_qo = (queue_obj_t *)((char *)Z_OBJ(zobj) - Z_OBJ(zobj)->handlers->offset);

                new_qo->qoi = old_qo->qoi;

                zend_string_free(ce_name);

                ZVAL_OBJ(value, Z_OBJ(zobj));
            }
            break;
        case PHT_HASH_TABLE:
            {
                zend_string *ce_name = zend_string_init("HashTable", sizeof("HashTable") - 1, 0);
                zend_class_entry *ce = zend_fetch_class_by_name(ce_name, NULL, ZEND_FETCH_CLASS_DEFAULT | ZEND_FETCH_CLASS_EXCEPTION);
                zval zobj;

                PHT_ZG(skip_htoi_creation) = 1;

                if (object_init_ex(&zobj, ce) != SUCCESS) {
                    // @todo this will throw an exception in the new thread, rather than at
                    // the call site - how should it behave?
                    zend_throw_exception(zend_ce_exception, "Failed to threaded object from HashTable class", 0);
                }

                PHT_ZG(skip_htoi_creation) = 0;

                hashtable_obj_t *old_hto = (hashtable_obj_t *)((char *)&ENTRY_HT(e)->obj - ENTRY_HT(e)->obj.handlers->offset);
                hashtable_obj_t *new_hto = (hashtable_obj_t *)((char *)Z_OBJ(zobj) - Z_OBJ(zobj)->handlers->offset);

                new_hto->htoi = old_hto->htoi;

                zend_string_free(ce_name);

                ZVAL_OBJ(value, Z_OBJ(zobj));
            }
            break;
        case IS_OBJECT:
            {
                size_t buf_len = PHT_STRL(ENTRY_STRING(e));
                const unsigned char *p = (const unsigned char *) PHT_STRV(ENTRY_STRING(e));
                php_unserialize_data_t var_hash;

                PHP_VAR_UNSERIALIZE_INIT(var_hash);

                if (!php_var_unserialize(value, &p, p + buf_len, &var_hash)) {
                    // @todo handle serialisation failure - is this even possible to hit?
                }

                PHP_VAR_UNSERIALIZE_DESTROY(var_hash);
            }
    }
}

void pht_convert_zval_to_entry(entry_t *e, zval *value)
{
    ENTRY_TYPE(e) = Z_TYPE_P(value);

    switch (Z_TYPE_P(value)) {
        case IS_STRING:
            PHT_STRL(ENTRY_STRING(e)) = ZSTR_LEN(Z_STR_P(value));
            PHT_STRV(ENTRY_STRING(e)) = malloc(sizeof(char) * PHT_STRL(ENTRY_STRING(e)));
            memcpy(PHT_STRV(ENTRY_STRING(e)), ZSTR_VAL(Z_STR_P(value)), sizeof(char) * PHT_STRL(ENTRY_STRING(e)));
            break;
        case IS_LONG:
            ENTRY_LONG(e) = Z_LVAL_P(value);
            break;
        case IS_DOUBLE:
            ENTRY_DOUBLE(e) = Z_DVAL_P(value);
            break;
        case _IS_BOOL:
            ENTRY_BOOL(e) = !!Z_LVAL_P(value);
            break;
        case IS_ARRAY:
            {
                smart_str smart = {0};
                php_serialize_data_t vars;

                PHP_VAR_SERIALIZE_INIT(vars);
                php_var_serialize(&smart, value, &vars);
                PHP_VAR_SERIALIZE_DESTROY(vars);

                if (EG(exception)) {
                    smart_str_free(&smart);
                    ENTRY_TYPE(e) = PHT_SERIALISATION_FAILED;
                } else {
                    zend_string *sval = smart_str_extract(&smart);

                    PHT_STRL(ENTRY_STRING(e)) = ZSTR_LEN(sval);
                    PHT_STRV(ENTRY_STRING(e)) = malloc(ZSTR_LEN(sval));
                    memcpy(PHT_STRV(ENTRY_STRING(e)), ZSTR_VAL(sval), ZSTR_LEN(sval));

                    zend_string_free(sval);
                }
            }
            break;
        case IS_OBJECT:
            {
                if (instanceof_function(Z_OBJCE_P(value), zend_ce_closure)) {
                    ENTRY_TYPE(e) = PHT_STORE_FUNC;
                    ENTRY_FUNC(e) = malloc(sizeof(zend_op_array));
                    memcpy(ENTRY_FUNC(e), zend_get_closure_method_def(value), sizeof(zend_op_array));
                    Z_ADDREF_P(value);
                } else if (instanceof_function(Z_OBJCE_P(value), Queue_ce)) {
                    queue_obj_t *qo = (queue_obj_t *)((char *)Z_OBJ_P(value) - Z_OBJ_P(value)->handlers->offset);

                    ENTRY_TYPE(e) = PHT_QUEUE;
                    ENTRY_Q(e) = qo;

                    pthread_mutex_lock(&qo->qoi->lock);
                    ++qo->qoi->refcount;
                    pthread_mutex_unlock(&qo->qoi->lock);
                } else if (instanceof_function(Z_OBJCE_P(value), HashTable_ce)) {
                    hashtable_obj_t *hto = (hashtable_obj_t *)((char *)Z_OBJ_P(value) - Z_OBJ_P(value)->handlers->offset);

                    ENTRY_TYPE(e) = PHT_HASH_TABLE;
                    ENTRY_HT(e) = hto;

                    pthread_mutex_lock(&hto->htoi->lock);
                    ++hto->htoi->refcount;
                    pthread_mutex_unlock(&hto->htoi->lock);
                } else {
                    // temporary solution - just serialise it and to the hell with the consequences
                    smart_str smart = {0};
                    php_serialize_data_t vars;

                    PHP_VAR_SERIALIZE_INIT(vars);
                    php_var_serialize(&smart, value, &vars);
                    PHP_VAR_SERIALIZE_DESTROY(vars);

                    if (EG(exception)) {
                        smart_str_free(&smart);
                        ENTRY_TYPE(e) = PHT_SERIALISATION_FAILED;
                    } else {
                        zend_string *sval = smart_str_extract(&smart);

                        PHT_STRL(ENTRY_STRING(e)) = ZSTR_LEN(sval);
                        PHT_STRV(ENTRY_STRING(e)) = malloc(ZSTR_LEN(sval));
                        memcpy(PHT_STRV(ENTRY_STRING(e)), ZSTR_VAL(sval), ZSTR_LEN(sval));

                        zend_string_free(sval);
                    }
                }
            }
            break;
        default:
            ENTRY_TYPE(e) = PHT_SERIALISATION_FAILED;
    }
}

void pht_entry_update(entry_t *entry, zval *value)
{
    pht_entry_delete_value(entry);
    pht_convert_zval_to_entry(entry, value);
}

entry_t *create_new_entry(zval *value)
{
    entry_t *e = malloc(sizeof(entry_t));

    pht_convert_zval_to_entry(e, value);

    return e;
}
