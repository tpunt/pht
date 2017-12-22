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

/*
Current API
*/

// $mq = new MessageQueue();
// $tr = new ThreadRef(Task::class, $mq);
//
// $tr->start();
//
// while (!$mq->isFinished() || $mq->hasMessages()) {
//     if ($mq->pop($message)) {
//         var_dump($message);
//     }
// }
//
// $tr->join();

/*
New API?
Thread reuse example
*/

// $mq = new MessageQueue();
// $thread = new Thread();
// $tasksRemaining = 2;
//
// $thread->addTask(Task::class, $mq);
// $thread->addTask(Task::class, $mq);
//
// $thread->start();
//
// while ($tasksRemaining) {
//     if ($mq->pop($message)) {
//         --$tasksRemaining;
//         var_dump($message);
//     }
// }
//
// $thread->join();

/*
New API?
Pool example
*/
