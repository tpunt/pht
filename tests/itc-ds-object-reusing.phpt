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
--EXPECT--
object(Queue)#2 (2) {
  [0]=>
  int(1)
  [1]=>
  int(2)
}
