<?php

use pht\{Thread, Vector};

const BAILOUT = 16;
const MAX_ITERATIONS = 3000;
const THREAD_COUNT = 1; // change me

class Pool
{
    private $poolSize = 0;
    private $threads = [];

    public function __construct(int $poolSize)
    {
        if ($poolSize < 1) {
            throw new Exception("Invalid pool size given");
        }

        $this->poolSize = $poolSize;

        for ($i = 0; $i < $this->poolSize; ++$i) {
            $this->threads[] = new Thread();
            $this->threads[$i]->start();
        }
    }

    public function addFunctionTask(string $fn, ...$fnArgs) : void
    {
        static $i = 0;

        $this->threads[$i]->addFunctionTask($fn, ...$fnArgs);

        $i = ($i + 1) % $this->poolSize;
    }

    public function close() : void
    {
        for ($i = 0; $i < $this->poolSize; ++$i) {
            $this->threads[$i]->join();
        }
    }
}

function iterate($x, $y, $results, $ri) : void
{
    $cr = $y - 0.5;
    $ci = $x;
    $zr = 0.0;
    $zi = 0.0;
    $i = 0;

    while (true) {
        ++$i;
        $temp = $zr * $zi;
        $zr2 = $zr * $zr;
        $zi2 = $zi * $zi;
        $zr = $zr2 - $zi2 + $cr;
        $zi = $temp + $temp + $ci;

        // we don't technically need to mutex lock $results (below) when
        // updating it, since we don't update the same memory location in
        // different threads

        if ($zi2 + $zr2 > BAILOUT) {
            // $results->lock();
            $results[$ri] = $i;
            // $results->unlock();
            return;
        }

        if ($i > MAX_ITERATIONS) {
            // $results->lock();
            $results[$ri] = 0;
            // $results->unlock();
            return;
        }
    }
}

$pool = new Pool(THREAD_COUNT);
$results = new Vector(78 * 78);

$start = microtime(true); // just time the computation

for ($y = -39; $y < 39; ++$y) {
    for ($x = -39; $x < 39; ++$x) {
        $pool->addFunctionTask('iterate', $x / 40.0, $y / 40.0, $results, ($y + 39) * 78 + $x + 39);
    }
}

$pool->close(); // block until all tasks have finished

$end = microtime(true);

for ($i = 0; $i < 78 * 78; ++$i) {
    echo $results[$i] === 0 ? '*' : ' ';

    if ($i % 78 === 0) {
        echo PHP_EOL;
    }
}

printf("\nTime taken: %s\n", $end - $start);
