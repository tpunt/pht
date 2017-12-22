<?php

class Task implements Threaded
{
    private $mq;

    public function __construct(MessageQueue $mq)
    {
        $this->mq = $mq;
    }

    public function run()
    {
        $this->mq->push(rand());
    }
}

class Pool
{
    private $poolSize = 0;
    private $mq = NULL;
    private $threads = [];

    public function __construct(int $poolSize, MessageQueue $mq = NULL)
    {
        $this->poolSize = $poolSize; // must be > 0
        $this->mq = $mq;

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

$mq = new MessageQueue();
$pool = new Pool(5, $mq);
$taskCount = 10;

for ($i = 0; $i < $taskCount; ++$i) {
    $pool->addTask(Task::class, $mq);
}

for ($i = 0; $i < $taskCount; ) {
    if ($mq->pop($message)) {
        ++$i;
        var_dump($message);
    }
}

$pool->close();
