PHP_ARG_ENABLE(pht, whether to enable pht support,
[  --enable-pht           Enable pht support])

if test "$PHP_PHT" != "no"; then
  PHP_NEW_EXTENSION(pht, pht.c pht_copy.c pht_zend.c pht_entry.c pht_general.c, $ext_shared,, -DZEND_ENABLE_STATIC_TSRMLS_CACHE=1)
fi
