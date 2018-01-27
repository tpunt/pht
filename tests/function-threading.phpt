--TEST--
Ensure function threading works correctly.
--FILE--
<?php

use pht\Thread;

class Test2
{
    public static function run(){var_dump('Test 4');}
    public function run2(){var_dump('a');}
}

function a(){var_dump('Test 3');}

$thread = new Thread();

$thread->addFunctionTask(static function() {var_dump('Test 1');});
$thread->addFunctionTask(function() {var_dump('Test 2');});
$thread->addFunctionTask('array_map', function ($n) {var_dump($n);}, [1,2,3,4]);
$thread->addFunctionTask('a');
$thread->addFunctionTask(['Test2', 'run']);
$thread->addFunctionTask([new Test2, 'run']);

$thread->start();
$thread->join();
--EXPECT--
string(6) "Test 1"
string(6) "Test 2"
int(1)
int(2)
int(3)
int(4)
string(6) "Test 3"
string(6) "Test 4"
string(6) "Test 4"
