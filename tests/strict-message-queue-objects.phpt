--TEST--
Be strict on what Queue objects can and cannot do.
--FILE--
<?php

$q = new Queue();

try {
    $q->a = 1;
} catch (Error $e) {
    var_dump($e->getMessage());
}

try {
    var_dump($q->a);
} catch (Error $e) {
    var_dump($e->getMessage());
}
--EXPECT--
string(43) "Properties on Queue objects are not enabled"
string(43) "Properties on Queue objects are not enabled"
