<?php

class T implements Threaded
{
    public function __construct(MessageQueue $mq)
    {
        $this->mq = $mq;
    }

    public function run()
    {
        $this->mq->push(1);
        $this->mq->finish();
    }
}

/*
use a Thread class instead? (new Thread(...))
Define constants Thread::ALIVE, Thread::FINISHED, Thread::JOINED
*/

$mq = new MessageQueue();

$tr = new ThreadRef(T::class, $mq);

$tr->start();

$results = [];

while (!$mq->finished()) {
    if ($mq->pop($message)) {
        //
    }
}

$tr->join();

var_dump($results);

/*
Threaded::alive could return ALIVE, FINISHED, or JOINED.
If it returned FINISHED, then the next job submitted to it could be executed.
This would mean ThreadRef need to take a job (0 args).
The spawned thread would also need to have a queue of jobs to do.
*/

// shared memory approach (mutex-controlled queue)
// while ($tr->alive()) {
//     if ($tr->hasData()) {
//         $results[] = $tr->getData();
//     }
// }

// var_dump($tr->fetch(/* flags? */));

// message passing approach
// enforce send and receive methods in Threaded interface. Must maintain parent
// thread (or just let creator pass in any thread ref into constructor, since
// they should be serialisable anyway).
// receive method could be executed via ticks?

/*
$aliveCount = $threadCount;

while ($aliveCount) {
    for ($i = 0; $i < $threadCount; ++$i) { // array or singly-linked list?
        if ($threads[$i]->hasData()) { // if or while?
            $results[] = $threads[$i]->getData();
        } else {
            if (!$threads[$i]->alive()) {
                $threads[$i]->join();
                --$aliveCount;
            }
        }
    }
}
*/

/*
MessageQueue class is used when wanting to:
 - fetch results when not inside a `Threaded` context (e.g. for the main thread)
 - send data from m:n threads (m:1 or 1:m should use message passing for less contention over mutex locks)

Message passing should be used when:
 - wanting to send data from m:1 or 1:m threads
*/
