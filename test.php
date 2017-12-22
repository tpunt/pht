<?php

class T implements Threaded
{
    private $mq;

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

$mq = new MessageQueue();

$tr = new ThreadRef(T::class, $mq);

$tr->start();

while (!$mq->isFinished() || $mq->hasMessages()) {
    if ($mq->pop($message)) {
        var_dump($message);
    }
}

$tr->join();
