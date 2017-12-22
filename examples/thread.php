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

$mq = new MessageQueue();
$thread = new Thread();
$tasksRemaining = 2;

$thread->addTask(Task::class, $mq);
$thread->addTask(Task::class, $mq);

$thread->start();

while ($tasksRemaining) {
    if ($mq->pop($message)) {
        --$tasksRemaining;
        var_dump($message);
    }
}

$thread->join();
