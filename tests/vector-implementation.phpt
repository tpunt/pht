--TEST--
Testing the vector data structure implementation
--FILE--
<?php

use pht\{Thread, Runnable, Vector};

class Test implements Runnable
{
    private $v;

    public function __construct(Vector $v)
    {
        $this->v = $v;
    }

    public function run()
    {
        for ($i = 0; $i < 2; ++$i) {
            $this->v->lock();
            $this->v->push(1);
            $this->v->unlock();
        }
    }
}

$v = new Vector();
$thread = new Thread();
$expectedEntryCount = 2;

$thread->addTask(Test::class, $v);
$thread->start();

while (true) {
    $v->lock();

    if ($v->size() === 2) {
        $v->unlock();
        break;
    }

    $v->unlock();
}

$v->lock();
var_dump($v);
$v->unlock();

$thread->join();

$v->pop();
var_dump($v);
$v->unshift(2);
var_dump($v);
$v->pop();
$v->pop();
try {
    var_dump($v->pop());
} catch (Error $e) {
    var_dump($e->getMessage());
}
try {
    var_dump($v->shift());
} catch (Error $e) {
    var_dump($e->getMessage());
}
try {
    var_dump($v->deleteAt(0));
} catch (Error $e) {
    var_dump($e->getMessage());
}
$v[] = 0;
$v[] = 1;
var_dump($v);
var_dump(isset($v[1]), isset($v[0]), empty($v[1]), empty($v[0]));
unset($v[1]);
var_dump(isset($v[1]), empty($v[1]));
$v->insertAt(2, 0);
var_dump($v);
$v->updateAt(3, 1);
var_dump($v);
$v = new Vector(2, 100);
var_dump($v);
$v->push(101);
var_dump($v);
$v->updateAt(99, 0);
var_dump($v);
$v = new Vector(4, 1);
var_dump($v);
$v->resize(3);
var_dump($v);
$v->resize(5);
var_dump($v);
--EXPECT--
object(pht\Vector)#1 (2) {
  [0]=>
  int(1)
  [1]=>
  int(1)
}
object(pht\Vector)#1 (1) {
  [0]=>
  int(1)
}
object(pht\Vector)#1 (2) {
  [0]=>
  int(2)
  [1]=>
  int(1)
}
string(48) "Attempted to pop an element from an empty vector"
string(50) "Attempted to shift an element from an empty vector"
string(58) "Attempted to delete an element from an out-of-bounds index"
object(pht\Vector)#1 (2) {
  [0]=>
  int(0)
  [1]=>
  int(1)
}
bool(true)
bool(true)
bool(false)
bool(true)
bool(false)
bool(true)
object(pht\Vector)#1 (2) {
  [0]=>
  int(2)
  [1]=>
  int(0)
}
object(pht\Vector)#1 (2) {
  [0]=>
  int(2)
  [1]=>
  int(3)
}
object(pht\Vector)#4 (2) {
  [0]=>
  int(100)
  [1]=>
  int(100)
}
object(pht\Vector)#4 (3) {
  [0]=>
  int(100)
  [1]=>
  int(100)
  [2]=>
  int(101)
}
object(pht\Vector)#4 (3) {
  [0]=>
  int(99)
  [1]=>
  int(100)
  [2]=>
  int(101)
}
object(pht\Vector)#1 (4) {
  [0]=>
  int(1)
  [1]=>
  int(1)
  [2]=>
  int(1)
  [3]=>
  int(1)
}
object(pht\Vector)#1 (3) {
  [0]=>
  int(1)
  [1]=>
  int(1)
  [2]=>
  int(1)
}
object(pht\Vector)#1 (5) {
  [0]=>
  int(1)
  [1]=>
  int(1)
  [2]=>
  int(1)
  [3]=>
  int(0)
  [4]=>
  int(0)
}
