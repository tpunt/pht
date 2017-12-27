--TEST--
Testing the HT data structure implementation
--FILE--
<?php

class Test implements Threaded
{
    private $ht, $str;

    public function __construct(HashTable $ht, string $str)
    {
        $this->ht = $ht;
        $this->str = $str;
    }

    public function run()
    {
        $this->ht->lock();
        for ($i = 0; $i < 2; ++$i) {
            $this->ht[$this->str] = 'def';
            $this->ht[0] = 'defg';
            $this->ht[1] = 'defg';
        }
        $this->ht->unlock();
    }
}

$thread = new Thread();
$ht = new HashTable();

$thread->addTask(Test::class, $ht, 'abc');
$thread->start();

while (true) {
    $ht->lock();
    if (count($ht) === 3) {
        $ht->unlock();
        break;
    }
    $ht->unlock();
}

$ht->lock();
var_dump($ht);
var_dump($ht['abc']);
foreach ($ht as $key => $val) {
    var_dump("$key => $val");
}
$ht->unlock();

$thread->join();
--EXPECT--
object(HashTable)#2 (3) {
  [0]=>
  string(4) "defg"
  [1]=>
  string(4) "defg"
  ["abc"]=>
  string(3) "def"
}
string(3) "def"
string(9) "0 => defg"
string(9) "1 => defg"
string(10) "abc => def"
