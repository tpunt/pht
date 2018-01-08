--TEST--
Ensure correct copying of local static variables.
--FILE--
<?php

class Test implements Runnable
{
    public function run(){}
}

$thread = new thread();

try {
    $thread->addTask(Test::class, fopen(__FILE__, 'r'));
} catch (Error $e) {
    var_dump($e->getMessage());
}
--EXPECT--
string(51) "Failed to serialise argument 1 of Thread::addTask()"
