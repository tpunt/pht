#ifndef PHT_ZEND_H
#define PHT_ZEND_H

ZEND_API HashTable* ZEND_FASTCALL pht_zend_array_dup(HashTable *source);
ZEND_API zend_ast *pht_zend_ast_copy(zend_ast *ast);

#endif
