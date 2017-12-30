--TEST--
Testing nesting a thread.
--FILE--
<?php

class Test2 implements Threaded
{
    public function run()
    {
        var_dump('Nested thread!');
    }
}

class Test implements Threaded
{
    public function run()
    {
        $thread = new Thread();

        $thread->addTask(Test2::class);
        $thread->start();
        $thread->join();
    }
}

$thread = new Thread();

$thread->addTask(Test::class);
$thread->start();
$thread->join();
--EXPECT--
string(14) "Nested thread!"
