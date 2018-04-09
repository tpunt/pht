PHP_ARG_ENABLE(pht, whether to enable pht support,
[  --enable-pht           Enable pht support])

if test "$PHP_PHT" != "no"; then
    PHP_NEW_EXTENSION(pht, pht.c \
        src/pht_copy.c \
        src/pht_zend.c \
        src/pht_entry.c \
        src/pht_string.c \
        src/ds/pht_queue.c \
        src/ds/pht_hashtable.c \
        src/ds/pht_vector.c \
        src/classes/thread.c \
        src/classes/threaded.c \
        src/classes/runnable.c \
        src/classes/queue.c \
        src/classes/hashtable.c \
        src/classes/vector.c \
        src/classes/atomic_integer.c, $ext_shared,, -DZEND_ENABLE_STATIC_TSRMLS_CACHE=1)

    EXTRA_CFLAGS="$EXTRA_CFLAGS -std=gnu99"
    PHP_SUBST(EXTRA_CFLAGS)
fi
