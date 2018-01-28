<?php

use pht\{Thread, Runnable, Queue};

class Task implements Runnable
{
    private $q;

    public function __construct(Queue $q)
    {
        $this->q = $q;
    }

    public function run()
    {
        $this->q->lock();
        $this->q->push(rand());
        $this->q->unlock();
    }
}

$q = new Queue();
$thread = new Thread();
$tasksRemaining = 2;

$thread->addClassTask(Task::class, $q);
$thread->addClassTask(Task::class, $q);

$thread->start();

while ($tasksRemaining) {
    $q->lock();

    if ($q->size()) {
        var_dump($q->pop());
        --$tasksRemaining;
    }

    $q->unlock();
}

$thread->join();
