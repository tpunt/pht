# New Threading POC

This is a POC implementation for a new approach to threading in PHP. As such, the implementation is very rough (leaky and incomplete) and I've only tested it against PHP 7.2. Everything and anything is subject to change still.

The basic idea is that `Threaded` objects are abstracted away behind references to such objects. This means that when we would like to create a new thread, we now do something such as:
```php
<?php

class SomeClass implements Threaded
{
    public function run() {}
}

$thread = new Thread();

// Thread::addTask(string $className, mixed ...$constructorArgs)
$thread->addTask(SomeClass::class);
```

By taking this approach, `Threaded` objects themselves no longer require their properties to be serialised. This is because we no longer need to pass around `Threaded` objects anymore, and so `Threaded` objects no longer need to be able to operate in multiple execution contexts.

This introduces a new problem: how can inter-thread communication (ITC) be performed? To solve this problem, I initially looked at a few inter-process communication (IPC) techniques. I first tried messages queues for ITC (with mutex locks hidden away internally), however after some playing around with it, the limited communication style of using queues only seemed inconvenient. Because I still liked the idea of using an explicit data structure for ITC (but simply wanted more flexibility with ITC data passing), I decided to implement other data structures and expose their mutex locks to the programmer for better control. Whilst this move increased complexity for the programmer, it also drastically increased the flexibility of what could be done by the programmer when using threads (which was what the overall aim of this extension was).

The following data structures have been implemented so far:
 - Queue (e.g. for a producer-consumer approach)
 - Hash table (e.g. for data tracking)

These data structures can be passed around between threads safely, and manipulated by multiple threads using the mutex locks that have been packed in with the data structure.

For example, a `Queue` may be used as follows:
```php
<?php

class Task implements Threaded
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

So with this approach to thread, only the given built-in data structures need to be safely passed around.

See also the [examples](https://github.com/tpunt/pht/tree/master/examples) folder.

## API

Quick overview:
```php
interface Threaded
{
    public function run() void;
}

class Queue
{
    public function push(mixed $value) : void;
    public function pop(void) : mixed;
    public function lock(void) : void;
    public function unlock(void) : void;
    public function size(void) : bool;
}

class HashTable
{
    public function lock(void) : void;
    public function unlock(void) : void;
}

class Thread
{
    public function addTask(string $className, mixed ...$ctorArgs);
    public function taskCount(void) : int;
    public function threadStatus(void) : int;
    public function start(void) : void;
    public function join(void) : void;
}
```

`Threaded` interface:
```php
interface Threaded
{
    /*
     * The entry point function for a new Threaded task.
     */
    public function run() void;
}
```

`Queue` class is reference-counted across threads, and so it does not need to be explicitly destroyed. It looks as follows:
```php
class Queue
{
    /*
     * Pushes a value to the queue.
     */
    public function push(mixed $value) : void;

    /*
     * Pops a value from the queue.
     */
    public function pop(void) : mixed;

    /*
     * Locks the mutex associated with this queue.
     */
    public function lock(void) : void;

    /*
     * Unlocks the mutex associated with this queue.
     */
    public function unlock(void) : void;

    /*
     * Returns the number of values in the queue.
     */
    public function size(void) : int;
}
```

`HashTable` class is reference-counted across threads, and so it does not need to be explicitly destroyed. It looks as follows:
```php
class HashTable
{
    /*
     * Locks the mutex associated with this hash table.
     */
    public function lock(void) : void;

    /*
     * Unlocks the mutex associated with this hash table.
     */
    public function unlock(void) : void;
}
```

`Thread` class:
```php
class Thread
{
    /*
     * Adds a new task to the thread.
     *
     * The arguments here will all be serialised, and the constructor will be
     * invoked in the thread itself (where the object will also be created).
     */
    public function addTask(string $className, mixed ...$ctorArgs);

    /*
     * Returns a count of the number of tasks the thread has.
     */
    public function taskCount(void) : int;

    /*
     * Returns the current status of a thread.
     *
     * Valid statuses: UNDER_CONSTRUCTION, ACTIVE, FINISHED
     */
    public function threadStatus(void) : int;

    /*
     * Starts the thread.
     *
     * This is where a new context is created. Any tasks lined up will begin
     * being processed.
     */
    public function start(void) : void;

    /*
     * Joins the thread.
     *
     * Destroys the newly created context, along with any tasks associated with
     * the thread.
     */
    public function join(void) : void;
}
```

With the above in mind, the serialisation points are:
 - The arguments to the `Thread::addTask` function
 - The values placed into the ITC-based data structures
