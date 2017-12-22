# New Threading POC

This is a POC implementation of a new threading approach. As such, the implementation is very rough (leaky, incomplete) and I've only tested it against PHP 7.2. Everything and anything is subject to change still.

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

This introduces a new problem: how can inter-thread communication be performed? To solve this problem, I decided to make threads "process-like" in terms of their communication between each another. The inter-process communication (IPC) technique I have decided to implement so far is message queues. Message queues are mutex-controlled queues that can be passed around between threads, enabling for a producer-consumer communication style.

In code, this looks like the following:
```php
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
        $this->mq->push(1);
        $this->mq->finish();
    }
}

$mq = new MessageQueue();
$thread = new Thread();

$thread->start();
$thread->addTask(Task::class, $mq);

while (!$mq->isFinished() || $mq->hasMessages()) {
    if ($mq->pop($message)) {
        var_dump($message);
    }
}

$thread->join();
```

So now, only message queues need to be safely passed around.

See also the [examples](https://github.com/tpunt/pht/tree/master/examples) folder.

## API

Quick overview:
```php
interface Threaded
{
    public function run() void;
}

class MessageQueue
{
    public function finish(void) : void;
    public function isFinished(void) : bool;
    public function push(mixed $message) : void;
    public function pop(mixed &$message) : bool;
    public function hasMessages(void) : bool;
}

class Thread // previously ThreadRef
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

`MessageQueue` class:
```php
class MessageQueue
{
    /*
     * Set the message queue to a finished state.
     *
     * This notifies threads that are using it that no new messages will be
     * pushed to the queue
     */
    public function finish(void) : void;

    /*
     * Checks to see if the queue has a finished state.
     */
    public function isFinished(void) : bool;

    /*
     * Pushes a message to the queue.
     */
    public function push(mixed $message) : void;

    /*
     * Pops a value from the queue.
     *
     * By using a boolean return value to denote whether a value was popped from
     * the queue or not, with a reference-based parameter to update on success,
     * we avoid having to expose a mutex lock to PHP developers. Otherwise, the
     * following would need to be performed: acquire a lock, check for presence
     * of messages, pop a message if present, release lock. So this approach
     * hides away the need for mutexes.
     */
    public function pop(mixed &$message) : bool;

    /*
     * Checks to see if any messages are left in the queue
     *
     * This is used to continue fetching messages from a queue even when it has
     * finished.
     */
    public function hasMessages(void) : bool;
}
```

`Thread` class:
```php
class Thread // previously ThreadRef
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
 - The values pushed to the message queue

In future, I may implement other IPC techniques, too (such as message passing). The need for additional communication techniques will hopefully become clearer in future.
