--TEST--
Ensure file threading works correctly.
--FILE--
<?php

use pht\{FileThread, Vector, HashTable, Queue};

$vector = new Vector();
$hashTable = new HashTable();
$queue = new Queue();

$ft = new FileThread('file_threading_helper.php', $vector, $hashTable, $queue);

$ft->start();
$ft->join();

var_dump($vector, $hashTable, $queue);
--EXPECT--
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
