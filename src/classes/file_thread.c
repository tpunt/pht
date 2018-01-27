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
#include <Zend/zend_API.h>
#include <Zend/zend_interfaces.h>

#include "php_pht.h"
#include "src/pht_thread.h"
#include "src/classes/file_thread.h"

zend_object_handlers file_thread_handlers;
zend_class_entry *FileThread_ce;

static zend_object *file_thread_ctor(zend_class_entry *entry)
{
    thread_obj_t *thread = ecalloc(1, sizeof(thread_obj_t) + zend_object_properties_size(entry));

    thread_init(thread, FILE_THREAD);

    zend_object_std_init(&thread->obj, entry);
    object_properties_init(&thread->obj, entry);

    thread->obj.handlers = &file_thread_handlers;

    zend_hash_index_add_ptr(&PHT_ZG(child_threads), (zend_ulong)thread, thread);

    return &thread->obj;
}

ZEND_BEGIN_ARG_INFO_EX(FileThread___construct_arginfo, 0, 0, 1)
ZEND_ARG_INFO(0, filename)
ZEND_END_ARG_INFO()

PHP_METHOD(FileThread, __construct)
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
                zend_throw_error(NULL, "Failed to serialise argument %d of Thread::addTask()", i + 1);

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

    pht_queue_push(&thread->tasks, task);
}

ZEND_BEGIN_ARG_INFO_EX(FileThread_start_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(FileThread, start)
{
    thread_obj_t *thread = (thread_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    thread->status = STARTING_UP;

    pthread_create((pthread_t *)thread, NULL, (void *)worker_function, thread);
}

ZEND_BEGIN_ARG_INFO_EX(FileThread_join_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(FileThread, join)
{
    thread_obj_t *thread = (thread_obj_t *)((char *)Z_OBJ(EX(This)) - Z_OBJ(EX(This))->handlers->offset);

    if (zend_parse_parameters_none() != SUCCESS) {
        return;
    }

    thread->status = JOINED;

    pthread_join(thread->thread, NULL);
}

zend_function_entry FileThread_methods[] = {
    PHP_ME(FileThread, __construct, FileThread___construct_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(FileThread, start, FileThread_start_arginfo, ZEND_ACC_PUBLIC)
    PHP_ME(FileThread, join, FileThread_join_arginfo, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

void file_thread_ce_init(void)
{
    zend_class_entry ce;
    zend_object_handlers *zh = zend_get_std_object_handlers();

    INIT_CLASS_ENTRY(ce, "pht\\FileThread", FileThread_methods);
    FileThread_ce = zend_register_internal_class(&ce);
    FileThread_ce->create_object = file_thread_ctor;
    FileThread_ce->ce_flags |= ZEND_ACC_FINAL;
    FileThread_ce->serialize = zend_class_serialize_deny;
    FileThread_ce->unserialize = zend_class_unserialize_deny;

    memcpy(&file_thread_handlers, zh, sizeof(zend_object_handlers));

    file_thread_handlers.offset = XtOffsetOf(thread_obj_t, obj);
    file_thread_handlers.free_obj = th_free_obj;
}
