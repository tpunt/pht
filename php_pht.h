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

#ifndef PHP_PHT_H
#define PHP_PHT_H

#include <Zend/zend_modules.h>

extern zend_module_entry pht_module_entry;
#define phpext_pht_ptr &pht_module_entry

#define PHP_PHT_VERSION "0.1.0"

#ifdef PHP_WIN32
#  define PHP_PHT_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#  define PHP_PHT_API __attribute__ ((visibility("default")))
#else
#  define PHP_PHT_API
#endif

#ifndef ZTS
#  error "ZTS is required"
#endif

#include <main/php.h>
#include <main/php_globals.h>
#include <main/php_ini.h>
#include <main/php_main.h>
#include <main/php_network.h>
#include <main/php_ticks.h>

#include <Zend/zend.h>
#include <Zend/zend_closures.h>
#include <Zend/zend_compile.h>
#include <Zend/zend_exceptions.h>
#include <Zend/zend_extensions.h>
#include <Zend/zend_globals.h>
#include <Zend/zend_hash.h>
#include <Zend/zend_inheritance.h>
#include <Zend/zend_interfaces.h>
#include <Zend/zend_list.h>
#include <Zend/zend_modules.h>
#include <Zend/zend_object_handlers.h>
#include <Zend/zend_smart_str.h>
#include <Zend/zend_ts_hash.h>
#include <Zend/zend_types.h>
#include <Zend/zend_variables.h>
#include <Zend/zend_vm.h>

#include <ext/standard/basic_functions.h>
#include <ext/standard/info.h>
#include <ext/standard/php_rand.h>
#include <ext/standard/php_var.h>

#include "TSRM.h"

#include <pthread.h>

#include "pht_general.h"
#include "pht_entry.h"
#include "pht_queue.h"

ZEND_BEGIN_MODULE_GLOBALS(pht)
    HashTable interned_strings; // used for op_array file names
    zend_bool skip_mqi_creation;
ZEND_END_MODULE_GLOBALS(pht)

ZEND_EXTERN_MODULE_GLOBALS(pht)

#define PHT_ZG(v) ZEND_MODULE_GLOBALS_ACCESSOR(pht, v)

#define PHT_CTX(ls, id, type, element) (((type) (*((void ***) ls))[TSRM_UNSHUFFLE_RSRC_ID(id)])->element)
#define PHT_EG(ls, v) PHT_CTX(ls, executor_globals_id, zend_executor_globals*, v)
#define PHT_CG(ls, v) PHT_CTX(ls, compiler_globals_id, zend_compiler_globals*, v)
#define PHT_SG(ls, v) PHT_CTX(ls, sapi_globals_id, sapi_globals_struct*, v)

#if defined(ZTS) && defined(COMPILE_DL_PHT)
ZEND_TSRMLS_CACHE_EXTERN()
#endif

typedef enum _status_t {
    UNDER_CONSTRUCTION,
    ACTIVE,
    FINISHED,
    DESTROYED // JOINED?
} status_t;

typedef struct _message_t {
    entry_t *message;
    struct _message_t *next;
} message_t;

typedef struct _task_t {
    pht_string_t class_name;
    int class_ctor_argc;
    entry_t *class_ctor_args;
} task_t;

typedef struct _message_queue_internal_t {
    pthread_mutex_t lock;
    zend_long state;
    uint32_t refcount;
    // queue_t messages;
    message_t *messages;
    message_t *last_message; // prevents traversing all messages when enqueueing
} message_queue_internal_t;

typedef struct _message_queue_t {
    message_queue_internal_t *mqi;
    zend_object obj;
} message_queue_t;

typedef struct _thread_t {
    pthread_t thread; // must be first member
    int tid; // use thread as global in tls instead?
    zend_ulong id; // local storage ID used to fetch local storage data
    pthread_mutex_t lock;
    status_t status;
    queue_t tasks;
    // pht_string_t class_name;
    // int class_ctor_argc;
    // entry_t *class_ctor_args;
    void*** ls; // pointer to local storage in TSRM
    // zend_object *threaded_object;
    zend_object obj;
} thread_t;

typedef struct _threads_t {
    thread_t **thread_table;
    int size;
    int used;
    pthread_mutex_t lock;
} threads_t;

extern thread_t main_thread;
extern zend_class_entry *MessageQueue_ce;

void free_message_queue_internal(message_queue_internal_t *mqi);

#endif
