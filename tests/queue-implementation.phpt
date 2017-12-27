--TEST--
Testing the HT data structure implementation
--FILE--
<?php

class Test implements Threaded
{
    private $q;

    public function __construct(Queue $q)
    {
        $this->q = $q;
    }

    public function run()
    {
        for ($i = 0; $i < 2; ++$i) {
            $this->q->lock();
            $this->q->push(1);
            $this->q->unlock();
        }
    }
}

$q = new Queue();
$thread = new Thread();
$expectedEntryCount = 2;

$thread->start();
$thread->addTask(Test::class, $q);

for ($i = 0; $i < $expectedEntryCount; ) {
    $q->lock();

    if ($q->size()) {
        ++$i;
        var_dump($q->pop());
    }

    $q->unlock();
}

$thread->join();
--EXPECT--
int(1)
int(1)
