--TEST--
Be strict on what MessageQueue objects can and cannot do.
--FILE--
<?php

$mq = new MessageQueue();

try {
    $mq->a = 1;
} catch (Exception $e) {
    var_dump($e->getMessage());
}

try {
    var_dump($mq->a);
} catch (Exception $e) {
    var_dump($e->getMessage());
}
--EXPECT--
string(50) "Properties on MessageQueue objects are not enabled"
string(50) "Properties on MessageQueue objects are not enabled"
