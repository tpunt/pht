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

#include <main/php.h>
#include <Zend/zend_API.h>

#include "src/classes/threaded.h"

zend_class_entry *Threaded_ce;

ZEND_BEGIN_ARG_INFO_EX(Threaded_lock_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(Threaded_unlock_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

zend_function_entry Threaded_methods[] = {
    PHP_ABSTRACT_ME(Threaded, lock, Threaded_lock_arginfo)
    PHP_ABSTRACT_ME(Threaded, unlock, Threaded_unlock_arginfo)
    PHP_FE_END
};

int threaded_interface_gets_implemented(zend_class_entry *interface, zend_class_entry *implementor)
{
    if (implementor->type == ZEND_INTERNAL_CLASS) {
        return SUCCESS;
    }

    zend_throw_error(NULL, "The Threaded interface cannot be implemented by userland classes");

    return FAILURE;
}

void threaded_ce_init(void)
{
    zend_class_entry ce;

    INIT_CLASS_ENTRY(ce, "pht\\Threaded", Threaded_methods);
    Threaded_ce = zend_register_internal_interface(&ce);
    Threaded_ce->interface_gets_implemented = threaded_interface_gets_implemented;
}
