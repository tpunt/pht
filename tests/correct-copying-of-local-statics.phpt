--TEST--
Ensure correct copying of local static variables.
--FILE--
<?php

const A = 1;
const C = ['a', 1, 'B', false, 'D'];
const D = ['a', 1, 'B'];

class T implements Runnable
{
    public function run()
    {
        static $a = A;
        static $b = A . A;
        static $c = C + D;
    }
}

$t = new Thread();
$t->addTask(T::class);

$t->start();

while ($t->taskCount());

$t->join();
--EXPECT--
