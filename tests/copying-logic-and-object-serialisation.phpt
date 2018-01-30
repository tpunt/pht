--TEST--
Testing that serialising objects in the context copying code fails
--FILE--
<?php

use pht\Thread;

class A
{
    public $a;
    public static $b;

    public function __construct()
    {
        $this->a = new StdClass();
        self::$b = new StdClass();
    }
}

new A();

$thread = new Thread();
$a = new StdClass;
$thread->addFunctionTask(function () use ($a) {var_dump($a);});
$thread->start();
$thread->join();
--EXPECT--
NULL
