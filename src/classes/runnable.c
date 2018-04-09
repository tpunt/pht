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

#include "src/classes/runnable.h"

zend_class_entry *Runnable_ce;

ZEND_BEGIN_ARG_INFO_EX(Runnable_run_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

zend_function_entry Runnable_methods[] = {
    PHP_ABSTRACT_ME(Runnable, run, Runnable_run_arginfo)
    PHP_FE_END
};

void runnable_ce_init(void)
{
    zend_class_entry ce;

    INIT_CLASS_ENTRY(ce, "pht\\Runnable", Runnable_methods);
    Runnable_ce = zend_register_internal_interface(&ce);
}
