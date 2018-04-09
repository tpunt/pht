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

#ifndef PHT_ZEND_H
#define PHT_ZEND_H

HashTable* ZEND_FASTCALL pht_zend_array_dup(HashTable *source);
zend_ast *pht_zend_ast_copy(zend_ast *ast);
void ZEND_FASTCALL _pht_zval_copy_ctor_func(zval *zvalue ZEND_FILE_LINE_DC);

/*
A thread-safe version of ZVAL_DUP, where constant AST and array types are
properly copied for threaded environments.
*/
#define PHT_ZVAL_DUP(z, v)									\
	do {												\
		zval *_z1 = (z);								\
		const zval *_z2 = (v);							\
		zend_refcounted *_gc = Z_COUNTED_P(_z2);		\
		uint32_t _t = Z_TYPE_INFO_P(_z2);				\
		ZVAL_COPY_VALUE_EX(_z1, _z2, _gc, _t);			\
		if ((_t & ((IS_TYPE_REFCOUNTED|IS_TYPE_COPYABLE) << Z_TYPE_FLAGS_SHIFT)) != 0) { \
			if ((_t & (IS_TYPE_COPYABLE << Z_TYPE_FLAGS_SHIFT)) != 0) { \
				_pht_zval_copy_ctor_func(_z1 ZEND_FILE_LINE_CC); \
			} else {									\
				GC_REFCOUNT(_gc)++;						\
			}											\
		}												\
	} while (0)

#endif
