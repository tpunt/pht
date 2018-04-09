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

#include "src/pht_zend.h"

/*
This file contains copied pieces of functionality from ZE. Such pieces of
functionality have usually required minor tweaks in order to work safely with
this extension (namely with respect to performing hard copies of zvals).
*/

/*
The following is taken from Zend/zend_hash.c and changes the copying of array
values to use PHT_ZVAL_DUP rather than ZVAL_COPY_VALUE (as well as performing a
hard copy of string keys).
*/
static const uint32_t uninitialized_bucket[-HT_MIN_MASK] =
	{HT_INVALID_IDX, HT_INVALID_IDX};

#if ZEND_DEBUG

#define HT_OK					0x00
#define HT_IS_DESTROYING		0x40
#define HT_DESTROYED			0x80
#define HT_CLEANING				0xc0

static void _zend_is_inconsistent(const HashTable *ht, const char *file, int line)
{
	if (ht->u.v.consistency == HT_OK) {
		return;
	}
	switch (ht->u.v.consistency) {
		case HT_IS_DESTROYING:
			zend_output_debug_string(1, "%s(%d) : ht=%p is being destroyed", file, line, ht);
			break;
		case HT_DESTROYED:
			zend_output_debug_string(1, "%s(%d) : ht=%p is already destroyed", file, line, ht);
			break;
		case HT_CLEANING:
			zend_output_debug_string(1, "%s(%d) : ht=%p is being cleaned", file, line, ht);
			break;
		default:
			zend_output_debug_string(1, "%s(%d) : ht=%p is inconsistent", file, line, ht);
			break;
	}
	zend_bailout();
}
#define IS_CONSISTENT(a) _zend_is_inconsistent(a, __FILE__, __LINE__);
#define SET_INCONSISTENT(n) do { \
		(ht)->u.v.consistency = n; \
	} while (0)
#else
#define IS_CONSISTENT(a)
#define SET_INCONSISTENT(n)
#endif

static zend_always_inline int pht_zend_array_dup_element(HashTable *source, HashTable *target, uint32_t idx, Bucket *p, Bucket *q, int packed, int static_keys, int with_holes)
{
	zval *data = &p->val;

	if (with_holes) {
		if (!packed && Z_TYPE_INFO_P(data) == IS_INDIRECT) {
			data = Z_INDIRECT_P(data);
		}
		if (UNEXPECTED(Z_TYPE_INFO_P(data) == IS_UNDEF)) {
			return 0;
		}
	} else if (!packed) {
		/* INDIRECT element may point to UNDEF-ined slots */
		if (Z_TYPE_INFO_P(data) == IS_INDIRECT) {
			data = Z_INDIRECT_P(data);
			if (UNEXPECTED(Z_TYPE_INFO_P(data) == IS_UNDEF)) {
				return 0;
			}
		}
	}
/* Newly commented lines - we don't increment the refcount since we perform a hard copy instead
	do {
		if (Z_OPT_REFCOUNTED_P(data)) {
			if (Z_ISREF_P(data) && Z_REFCOUNT_P(data) == 1 &&
			    (Z_TYPE_P(Z_REFVAL_P(data)) != IS_ARRAY ||
			      Z_ARRVAL_P(Z_REFVAL_P(data)) != source)) {
				data = Z_REFVAL_P(data);
				if (!Z_OPT_REFCOUNTED_P(data)) {
					break;
				}
			}
			Z_ADDREF_P(data);
		}
	} while (0);*/
	PHT_ZVAL_DUP(&q->val, data); // Changed line here (from ZVAL_COPY_VALUE)

	q->h = p->h;
	if (packed) {
		q->key = NULL;
	} else {
		uint32_t nIndex;

		q->key = p->key;
		if (!static_keys && q->key) {
			q->key = zend_string_dup(q->key, 0); // Changed line here (duplicate the string key)
		}

		nIndex = q->h | target->nTableMask;
		Z_NEXT(q->val) = HT_HASH(target, nIndex);
		HT_HASH(target, nIndex) = HT_IDX_TO_HASH(idx);
	}
	return 1;
}

static zend_always_inline void pht_zend_array_dup_packed_elements(HashTable *source, HashTable *target, int with_holes)
{
	Bucket *p = source->arData;
	Bucket *q = target->arData;
	Bucket *end = p + source->nNumUsed;

	do {
		if (!pht_zend_array_dup_element(source, target, 0, p, q, 1, 1, with_holes)) {
			if (with_holes) {
				ZVAL_UNDEF(&q->val);
			}
		}
		p++; q++;
	} while (p != end);
}

static zend_always_inline uint32_t pht_zend_array_dup_elements(HashTable *source, HashTable *target, int static_keys, int with_holes)
{
	uint32_t idx = 0;
	Bucket *p = source->arData;
	Bucket *q = target->arData;
	Bucket *end = p + source->nNumUsed;

	do {
		if (!pht_zend_array_dup_element(source, target, idx, p, q, 0, static_keys, with_holes)) {
			uint32_t target_idx = idx;

			idx++; p++;
			while (p != end) {
				if (pht_zend_array_dup_element(source, target, target_idx, p, q, 0, static_keys, with_holes)) {
					if (source->nInternalPointer == idx) {
						target->nInternalPointer = target_idx;
					}
					target_idx++; q++;
				}
				idx++; p++;
			}
			return target_idx;
		}
		idx++; p++; q++;
	} while (p != end);
	return idx;
}

HashTable* ZEND_FASTCALL pht_zend_array_dup(HashTable *source)
{
	uint32_t idx;
	HashTable *target;

	IS_CONSISTENT(source);

	ALLOC_HASHTABLE(target);
	GC_REFCOUNT(target) = 1;
	GC_TYPE_INFO(target) = IS_ARRAY | (GC_COLLECTABLE << GC_FLAGS_SHIFT);

	target->nTableSize = source->nTableSize;
	target->pDestructor = source->pDestructor;

	if (source->nNumUsed == 0) {
		target->u.flags = (source->u.flags & ~(HASH_FLAG_INITIALIZED|HASH_FLAG_PACKED|HASH_FLAG_PERSISTENT|ZEND_HASH_APPLY_COUNT_MASK)) | HASH_FLAG_APPLY_PROTECTION | HASH_FLAG_STATIC_KEYS;
		target->nTableMask = HT_MIN_MASK;
		target->nNumUsed = 0;
		target->nNumOfElements = 0;
		target->nNextFreeElement = 0;
		target->nInternalPointer = HT_INVALID_IDX;
		HT_SET_DATA_ADDR(target, &uninitialized_bucket);
	} else if (GC_FLAGS(source) & IS_ARRAY_IMMUTABLE) {
		target->u.flags = (source->u.flags & ~HASH_FLAG_PERSISTENT) | HASH_FLAG_APPLY_PROTECTION;
		target->nTableMask = source->nTableMask;
		target->nNumUsed = source->nNumUsed;
		target->nNumOfElements = source->nNumOfElements;
		target->nNextFreeElement = source->nNextFreeElement;
		HT_SET_DATA_ADDR(target, emalloc(HT_SIZE(target)));
		target->nInternalPointer = source->nInternalPointer;
		memcpy(HT_GET_DATA_ADDR(target), HT_GET_DATA_ADDR(source), HT_USED_SIZE(source));
		if (target->nNumOfElements > 0 &&
		    target->nInternalPointer == HT_INVALID_IDX) {
			idx = 0;
			while (Z_TYPE(target->arData[idx].val) == IS_UNDEF) {
				idx++;
			}
			target->nInternalPointer = idx;
		}
	} else if (source->u.flags & HASH_FLAG_PACKED) {
		target->u.flags = (source->u.flags & ~(HASH_FLAG_PERSISTENT|ZEND_HASH_APPLY_COUNT_MASK)) | HASH_FLAG_APPLY_PROTECTION;
		target->nTableMask = source->nTableMask;
		target->nNumUsed = source->nNumUsed;
		target->nNumOfElements = source->nNumOfElements;
		target->nNextFreeElement = source->nNextFreeElement;
		HT_SET_DATA_ADDR(target, emalloc(HT_SIZE(target)));
		target->nInternalPointer = source->nInternalPointer;
		HT_HASH_RESET_PACKED(target);

		if (HT_IS_WITHOUT_HOLES(target)) {
			pht_zend_array_dup_packed_elements(source, target, 0);
		} else {
			pht_zend_array_dup_packed_elements(source, target, 1);
		}
		if (target->nNumOfElements > 0 &&
		    target->nInternalPointer == HT_INVALID_IDX) {
			idx = 0;
			while (Z_TYPE(target->arData[idx].val) == IS_UNDEF) {
				idx++;
			}
			target->nInternalPointer = idx;
		}
	} else {
		target->u.flags = (source->u.flags & ~(HASH_FLAG_PERSISTENT|ZEND_HASH_APPLY_COUNT_MASK)) | HASH_FLAG_APPLY_PROTECTION;
		target->nTableMask = source->nTableMask;
		target->nNextFreeElement = source->nNextFreeElement;
		target->nInternalPointer = source->nInternalPointer;

		HT_SET_DATA_ADDR(target, emalloc(HT_SIZE(target)));
		HT_HASH_RESET(target);

		if (HT_HAS_STATIC_KEYS_ONLY(target)) {
			if (HT_IS_WITHOUT_HOLES(source)) {
				idx = pht_zend_array_dup_elements(source, target, 1, 0);
			} else {
				idx = pht_zend_array_dup_elements(source, target, 1, 1);
			}
		} else {
			if (HT_IS_WITHOUT_HOLES(source)) {
				idx = pht_zend_array_dup_elements(source, target, 0, 0);
			} else {
				idx = pht_zend_array_dup_elements(source, target, 0, 1);
			}
		}
		target->nNumUsed = idx;
		target->nNumOfElements = idx;
		if (idx > 0 && target->nInternalPointer == HT_INVALID_IDX) {
			target->nInternalPointer = 0;
		}
	}
	return target;
}


/*
The following was taken from Zend/zend_ast.c:454 - a hard duplicate of the zval
needs to be performed

This is only applicable to PHP 7.2 (it needs updating for 7.3)
*/

#include "Zend/zend_ast.h"

static inline size_t zend_ast_size(uint32_t children) {
	return sizeof(zend_ast) - sizeof(zend_ast *) + sizeof(zend_ast *) * children;
}

static inline size_t zend_ast_list_size(uint32_t children) {
	return sizeof(zend_ast_list) - sizeof(zend_ast *) + sizeof(zend_ast *) * children;
}

zend_ast *pht_zend_ast_copy(zend_ast *ast)
{
	if (ast == NULL) {
		return NULL;
	} else if (ast->kind == ZEND_AST_ZVAL) {
		zend_ast_zval *new = emalloc(sizeof(zend_ast_zval));
		new->kind = ZEND_AST_ZVAL;
		new->attr = ast->attr;
		PHT_ZVAL_DUP(&new->val, zend_ast_get_zval(ast)); // Changed line (from ZVAL_COPY)
		return (zend_ast *) new;
	} else if (zend_ast_is_list(ast)) {
		zend_ast_list *list = zend_ast_get_list(ast);
		zend_ast_list *new = emalloc(zend_ast_list_size(list->children));
		uint32_t i;
		new->kind = list->kind;
		new->attr = list->attr;
		new->children = list->children;
		for (i = 0; i < list->children; i++) {
			new->child[i] = pht_zend_ast_copy(list->child[i]);
		}
		return (zend_ast *) new;
	} else {
		uint32_t i, children = zend_ast_get_num_children(ast);
		zend_ast *new = emalloc(zend_ast_size(children));
		new->kind = ast->kind;
		new->attr = ast->attr;
		for (i = 0; i < children; i++) {
			new->child[i] = pht_zend_ast_copy(ast->child[i]);
		}
		return new;
	}
}

/*
The following is taken from Zend/zend_variables.c and changes the way arrays and
constant AST types are copied. This internal function is used for PHT_ZVAL_DUP
(defined in pht_zend.h), which is a thread-safe version of ZVAL_DUP.
*/
void ZEND_FASTCALL _pht_zval_copy_ctor_func(zval *zvalue ZEND_FILE_LINE_DC)
{
	if (EXPECTED(Z_TYPE_P(zvalue) == IS_ARRAY)) {
		ZVAL_ARR(zvalue, pht_zend_array_dup(Z_ARRVAL_P(zvalue))); // changed line
	} else if (EXPECTED(Z_TYPE_P(zvalue) == IS_STRING)) {
		CHECK_ZVAL_STRING_REL(Z_STR_P(zvalue));
		ZVAL_NEW_STR(zvalue, zend_string_dup(Z_STR_P(zvalue), 0));
	} else if (EXPECTED(Z_TYPE_P(zvalue) == IS_CONSTANT)) {
		CHECK_ZVAL_STRING_REL(Z_STR_P(zvalue));
		Z_STR_P(zvalue) = zend_string_dup(Z_STR_P(zvalue), 0);
	} else if (EXPECTED(Z_TYPE_P(zvalue) == IS_CONSTANT_AST)) {
		zend_ast *copy = pht_zend_ast_copy(Z_ASTVAL_P(zvalue)); // changed line
		ZVAL_NEW_AST(zvalue, copy);
	}
}
