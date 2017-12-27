<?php

class Task implements Threaded
{
    private $q;

    public function __construct(Queue $q)
    {
        $this->q = $q;
    }

    public function run()
    {
        $this->q->push(rand());
    }
}

class Pool
{
    private $poolSize = 0;
    private $q = NULL;
    private $threads = [];

    public function __construct(int $poolSize, Queue $q = NULL)
    {
        $this->poolSize = $poolSize; // must be > 0
        $this->q = $q;

        for ($i = 0; $i < $this->poolSize; ++$i) {
            $this->threads[] = new Thread();
            $this->threads[$i]->start();
        }
    }

    public function addTask(string $className, ...$ctorArgs) : void
    {
        static $i = 0;

        $this->threads[$i]->addTask($className, ...$ctorArgs);

        $i = ($i + 1) % $this->poolSize;
    }

    public function close() : void
    {
        for ($i = 0; $i < $this->poolSize; ++$i) {
            $this->threads[$i]->join();
        }
    }
}

$q = new Queue();
$pool = new Pool(5, $q);
$taskCount = 10;

for ($i = 0; $i < $taskCount; ++$i) {
    $pool->addTask(Task::class, $q);
}

for ($i = 0; $i < $taskCount; ) {
    $q->lock();

    if ($q->size()) {
        var_dump($q->pop());
        ++$i;
    }

    $q->unlock();
}

$pool->close();
