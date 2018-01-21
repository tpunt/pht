# Pht: A New Approach to Threading in PHP

This extension is a new approach to threading in PHP. Caution: everything and anything is still subject to change.

Quick feature list:
 - Classes, functions, and files may be threaded
 - The inter-thread communication (ITC) data structures include: hash table, queue, vector
 - Threads are always reusable for any number of tasks

Requirements:
 - A ZTS version of PHP 7.2. The master branch of php-src is not currently compatible
 - A Unix-based OS. Windows is not currently supported

## The Basics

This approach to threading abstracts away the thread itself behind a dedicated object (either a `Thread` or `FileThread`).

If we are dealing with a `Thread`, then we add tasks to the thread's task queue:
```php
<?php

class Task implements Runnable
{
    public function run() {}
}

$thread = new Thread();

// Thread::addTask(string $className, mixed ...$constructorArgs)
$thread->addTask(Task::class);
$thread->addFunctionTask(function () {});

$thread->start();
$thread->join();
```

The class to be threaded will be instantiated inside of the new thread, where it will execute in isolation without being passed around between threads.

If we are dealing with a `FileThread`, then we simply specify the name of the file we would like to thread:
```php
<?php

// FileThread::__construct(string $filename, mixed ...$globals);
$fileThread = new FileThread('some_file.php', 1, 2, 3);

$fileThread->start();
$fileThread->join();


/* some_file.php */
<?php

[$one, $two, $three] = $_THREAD;
```

By keeping the threading contexts completely separate from one-another, we prevent the need to serialise the properties of threaded objects (a necessary evil if such objects had to operate in multiple threads, as seen in pthreads).

Given the isolation of threaded contexts, we have a new problem: how can data be passed between threads? To solve this problem, threadable data structures have been implemented, where mutex locks have been exposed to the programmer for greater control over them. Whilst this has increased the complexity a bit for the programmer, it has also increased the flexibility, too.

So far, the following data structures have been implemented: queue, hash table, vector. These data structures can be safely passed around between threads, and manipulated by multiple threads using the mutex locks that have been packed in with the data structure. They are reference-counted across threads, and so they do not need to be explicitly destroyed.

For example, a `Queue` may be used as follows:
```php
<?php

class Task implements Runnable
{
    private $queue;

    public function __construct(Queue $queue)
    {
        $this->queue = $queue;
    }

    public function run()
    {
        $this->queue->lock();
        $this->queue->push(1);
        $this->queue->unlock();
    }
}

$queue = new Queue();
$thread = new Thread();
$expectedResultCount = 1;

$thread->start();
$thread->addTask(Task::class, $queue);

while ($expectedResultCount) {
    $queue->lock();

    if ($queue->size()) {
        var_dump($queue->pop());
        --$expectedResultCount;
    }

    $queue->unlock();
}

$thread->join();
```

So with this approach to threading, only the given built-in data structures need to be safely passed around between threads.

This means that the serialisation points to be aware of are:
 - The arguments being passed to `Thread::addTask()` and `FileThread::__construct()`
 - The values being placed into the ITC-based data structures

See also the [examples](https://github.com/tpunt/pht/tree/master/examples) folder.

## API

Quick overview:
```php
class Thread
{
    public function addTask(string $className, mixed ...$ctorArgs);
    public function addFunctionTask(callable $fn, mixed ...$fnArgs);
    public function taskCount(void) : int;
    public function start(void) : void;
    public function join(void) : void;
}

class FileThread
{
    public function __construct(string $filename, mixed ...$globals);
    public function start(void) : void;
    public function join(void) : void;
}

interface Runnable
{
    public function run() void;
}

interface Threaded // internal interface, not implementable by userland PHP classes
{
    public function lock(void) : void;
    public function unlock(void) : void;
}

class Queue implements Threaded
{
    public function push(mixed $value) : void;
    public function pop(void) : mixed;
    public function front(void) : mixed;
    public function lock(void) : void;
    public function unlock(void) : void;
    public function size(void) : int;
}

class HashTable implements Threaded
{
    public function lock(void) : void;
    public function unlock(void) : void;
}

class Vector implements Threaded
{
    public function push(mixed $value) : void;
    public function pop(void) : mixed;
    public function shift(void) : mixed;
    public function unshift(mixed $value) : void;
    public function insertAt(mixed $value, int $index) : void;
    public function deleteAt(int $index) : void;
    public function lock(void) : void;
    public function unlock(void) : void;
    public function size(void) : int;
}
```
