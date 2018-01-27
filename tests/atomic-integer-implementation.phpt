--TEST--
Testing the AtomicInteger class implementation
--FILE--
<?php

use pht\{Thread, AtomicInteger};

$thread = new Thread();
$startingN = 5;
$atomicInteger = new AtomicInteger(5);
$aic = 10;

for ($i = 0; $i < $aic; ++$i) {
    $thread->addFunctionTask(function ($atomicInteger, $aic) {
        for ($i = 0; $i < $aic; ++$i) {
            $atomicInteger->inc();
        }
    }, $atomicInteger, $aic);
}

$thread->start();

// fast way - we don't do it this way to ensure the reentrant mutex works correctly
// while ($atomicInteger->get() !== $aic**2 + $startingN);

while (true) {
    $atomicInteger->lock();

    if ($atomicInteger->get() === $aic**2 + $startingN) {
        $atomicInteger->unlock();
        break;
    }

    $atomicInteger->unlock();
}

$thread->join();

var_dump($atomicInteger->get());
var_dump($atomicInteger);
$atomicInteger->inc();
var_dump($atomicInteger);
--EXPECT--
int(105)
object(pht\AtomicInteger)#2 (1) {
  ["value"]=>
  int(105)
}
object(pht\AtomicInteger)#2 (1) {
  ["value"]=>
  int(106)
}
