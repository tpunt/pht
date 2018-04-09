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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <main/php.h>
#include <main/SAPI.h>
#include <ext/standard/info.h>

#include "php_pht.h"
#include "src/classes/thread.h"
#include "src/classes/threaded.h"
#include "src/classes/runnable.h"
#include "src/classes/queue.h"
#include "src/classes/hashtable.h"
#include "src/classes/vector.h"
#include "src/classes/atomic_integer.h"

ZEND_DECLARE_MODULE_GLOBALS(pht)

static int (*sapi_module_deactivate)(void);

PHP_MINIT_FUNCTION(pht)
{
    threaded_ce_init();
    runnable_ce_init();
    thread_ce_init();
    queue_ce_init();
    hashtable_ce_init();
    vector_ce_init();
    atomic_integer_ce_init();

    sapi_module_deactivate = sapi_module.deactivate;
    sapi_module.deactivate = NULL;

    return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(pht)
{
    sapi_module.deactivate = sapi_module_deactivate;

    return SUCCESS;
}

PHP_RINIT_FUNCTION(pht)
{
    ZEND_TSRMLS_CACHE_UPDATE();

    zend_hash_init(&PHT_ZG(op_array_file_names), 8, NULL, ZVAL_PTR_DTOR, 0);
    zend_hash_init(&PHT_ZG(child_threads), 8, NULL, thread_join_destroy, 0);
    PHT_ZG(skip_qoi_creation) = 0;
    PHT_ZG(skip_htoi_creation) = 0;
    PHT_ZG(skip_voi_creation) = 0;
    PHT_ZG(skip_aioi_creation) = 0;

    return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(pht)
{
    zend_hash_destroy(&PHT_ZG(op_array_file_names));
    zend_hash_destroy(&PHT_ZG(child_threads));

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
