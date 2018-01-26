--TEST--
Testing the HT data structure implementation
--FILE--
<?php

class Test implements Runnable
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
            $this->ht[0] = 0;
            $this->ht[1] = null;
            $this->ht[2] = 1;
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
    if ($ht->size() === 4) {
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
var_dump(isset($ht['abc']), isset($ht['def']), isset($ht[0]), isset($ht[1]));
var_dump(empty($ht['abc']), empty($ht['def']), empty($ht[0]), empty($ht[1]), empty($ht[2]));
unset($ht['abc']);
var_dump($ht);
var_dump(isset($ht['abc']));
try {
    $ht[] = 1;
} catch (Error $e) {
    var_dump($e->getMessage());
}
$ht->unlock();

$thread->join();
--EXPECT--
object(HashTable)#2 (4) {
  [0]=>
  int(0)
  [1]=>
  NULL
  [2]=>
  int(1)
  ["abc"]=>
  string(3) "def"
}
string(3) "def"
string(6) "0 => 0"
string(5) "1 => "
string(6) "2 => 1"
string(10) "abc => def"
bool(true)
bool(false)
bool(true)
bool(false)
bool(false)
bool(true)
bool(true)
bool(true)
bool(false)
object(HashTable)#2 (3) {
  [0]=>
  int(0)
  [1]=>
  NULL
  [2]=>
  int(1)
}
bool(false)
string(39) "Empty offset insertions are not allowed"
