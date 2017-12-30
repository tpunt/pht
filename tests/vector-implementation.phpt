--TEST--
Testing the vector data structure implementation
--FILE--
<?php

class Test implements Threaded
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
--EXPECT--
object(Vector)#1 (2) {
  [0]=>
  int(1)
  [1]=>
  int(1)
}
object(Vector)#1 (1) {
  [0]=>
  int(1)
}
object(Vector)#1 (2) {
  [0]=>
  int(2)
  [1]=>
  int(1)
}
