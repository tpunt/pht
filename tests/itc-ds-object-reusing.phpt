--TEST--
Testing the object reusing logic for the ITC DSs
--FILE--
<?php

$thread = new Thread();
$q = new Queue();

$thread->addFunctionTask(function ($q) {$q->lock();$q->push(1);$q->unlock();}, $q);
$thread->addFunctionTask(function ($q) {$q->lock();$q->push(2);$q->unlock();}, $q);

$thread->start();
$thread->join();

var_dump($q);

$thread = new Thread();
$q = new Queue();
$ht = new HashTable();

$q->push($ht);

$thread->addFunctionTask(function ($q) {
    for ($i = 0; $i < 5; ++$i) {
        $q->lock();
        $ht = $q->front();
        $ht[$i] = $i;
        $q->unlock();
    }
}, $q);

$thread->start();
$thread->join();

var_dump($q, $ht);

--EXPECT--
object(Queue)#2 (2) {
  [0]=>
  int(1)
  [1]=>
  int(2)
}
object(Queue)#1 (1) {
  [0]=>
  object(HashTable)#4 (5) {
    [0]=>
    int(0)
    [1]=>
    int(1)
    [2]=>
    int(2)
    [3]=>
    int(3)
    [4]=>
    int(4)
  }
}
object(HashTable)#2 (5) {
  [0]=>
  int(0)
  [1]=>
  int(1)
  [2]=>
  int(2)
  [3]=>
  int(3)
  [4]=>
  int(4)
}
