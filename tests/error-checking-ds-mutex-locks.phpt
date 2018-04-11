--TEST--
Error checking mutex locks for ITC DSs
--FILE--
<?php

$v = new pht\Vector();
$ht = new pht\HashTable();
$q = new pht\Queue();

try {
    $v->unlock();
} catch (Error $e) {
    var_dump($e->getMessage());
}

$v->lock();

try {
    $v->lock();
} catch (Error $e) {
    var_dump($e->getMessage());
}

try {
    $ht->unlock();
} catch (Error $e) {
    var_dump($e->getMessage());
}

$ht->lock();

try {
    $ht->lock();
} catch (Error $e) {
    var_dump($e->getMessage());
}

try {
    $q->unlock();
} catch (Error $e) {
    var_dump($e->getMessage());
}

$q->lock();

try {
    $q->lock();
} catch (Error $e) {
    var_dump($e->getMessage());
}

$t = new pht\Thread();

$t->addFunctionTask(function ($v) {
    try {
        $v->unlock();
    } catch (Error $e) {
        var_dump($e->getMessage());
    }
}, $v);

$t->addFunctionTask(function ($ht) {
    try {
        $ht->unlock();
    } catch (Error $e) {
        var_dump($e->getMessage());
    }
}, $ht);

$t->addFunctionTask(function ($q) {
    try {
        $q->unlock();
    } catch (Error $e) {
        var_dump($e->getMessage());
    }
}, $q);

$t->start();
$t->join();
--EXPECT--
string(78) "This mutex lock is either unheld, or is currently being held by another thread"
string(52) "This mutex lock is already being held by this thread"
string(78) "This mutex lock is either unheld, or is currently being held by another thread"
string(52) "This mutex lock is already being held by this thread"
string(78) "This mutex lock is either unheld, or is currently being held by another thread"
string(52) "This mutex lock is already being held by this thread"
string(78) "This mutex lock is either unheld, or is currently being held by another thread"
string(78) "This mutex lock is either unheld, or is currently being held by another thread"
string(78) "This mutex lock is either unheld, or is currently being held by another thread"
