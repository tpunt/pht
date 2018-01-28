<?php

[$vector, $hashTable, $queue] = $_THREAD;

var_dump($vector, $hashTable, $queue);

$vector->lock();
$vector[] = 1;
$vector->unlock();

$hashTable->lock();
$hashTable['a'] = 1;
$hashTable->unlock();

$queue->lock();
$queue->push(1);
$queue->unlock();
