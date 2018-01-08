--TEST--
Ensure zvals are correctly copied on a class entry. This includes default
property and static property tables, along with class constants.
--FILE--
<?php

class Test2
{
    public const A = [
        __DIR__ . 'A',
        ['B' => 'c']
    ];

    public static $a = [
        __DIR__ . 'A',
        ['B' => 'c']
    ];

    public $b = [
        __DIR__ . 'A',
        ['B' => 'c']
    ];
}

class Test implements Runnable
{
    public function run()
    {
        var_dump(Test2::A, Test2::$a, (new Test2)->b);
        try {
            var_dump(Test2::B);
        } catch (Error $e) {
            var_dump($e->getMessage());
        }
    }
}

$thread = new thread();

$thread->addTask(Test::class);
$thread->start();
$thread->join();
--EXPECTF--
array(2) {
  [0]=>
  %s
  [1]=>
  array(1) {
    ["B"]=>
    string(1) "c"
  }
}
array(2) {
  [0]=>
  %s
  [1]=>
  array(1) {
    ["B"]=>
    string(1) "c"
  }
}
array(2) {
  [0]=>
  %s
  [1]=>
  array(1) {
    ["B"]=>
    string(1) "c"
  }
}
string(28) "Undefined class constant 'B'"
