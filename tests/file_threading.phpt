--TEST--
Ensure file threading works correctly.
--FILE--
<?php

use pht\{Thread, Vector, HashTable, Queue};

$vector = new Vector();
$hashTable = new HashTable();
$queue = new Queue();

$thread = new Thread();

for ($i = 0; $i < 2; ++$i) {
    $thread->addFileTask('file_threading_helper.php', $i + 1, $i + 2, $i + 3);
}

$thread->addFileTask('file_threading_helper2.php', $vector, $hashTable, $queue);

$thread->start();
$thread->join();

var_dump($vector, $hashTable, $queue);
--EXPECT--
int(1)
int(2)
int(3)
int(2)
int(3)
int(4)
object(pht\Vector)#1 (0) {
}
object(pht\HashTable)#2 (0) {
}
object(pht\Queue)#3 (0) {
}
object(pht\Vector)#1 (1) {
  [0]=>
  int(1)
}
object(pht\HashTable)#2 (1) {
  ["a"]=>
  int(1)
}
object(pht\Queue)#3 (1) {
  [0]=>
  int(1)
}
