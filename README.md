# New Threading POC

This is a POC implementation of a new threading approach. `Threaded` objects are abstracted away behind references to such objects. This means that when we would like to create a new thread, we now do something such as:
```php
<?php

class SomeClass implements Threaded
{
    public function run() {}
}

// ThreadRef::__construct(string $className, mixed ...$constructorArgs)
$tr = new ThreadRef(SomeClass::class);
```

By taking this approach, `Threaded` objects themselves no longer require their properties to be serialised. This is because we no longer need to pass around `Threaded` objects anymore (instead, their references can be safely passed around), and so `Threaded` objects no longer need to be able to operate in multiple execution contexts.

This introduces a new problem: how can inter-thread communication be performed? To solve this problem, I decided to make threads "process-like" in terms of their communication between each another. The inter-process communication (IPC) techniques I have decided to implement are:
 - shared memory, and
 - message passing

The shared memory approach is simply a mutex-controlled queue that is attached to a `Threaded` object, and is accessible from its corresponding reference object. The queue enables for a producer thread (such as a child thread) to enqueue processed results to it, and then a consumer thread (such as a parent thread) can dequeue such results from it.

In code, this looks like the following:
```php
<?php

class SomeClass implements Threaded
{
    public function run()
    {
        $this->push(1);
    }
}

// create a new thread
$tr = new ThreadRef(SomeClass::class);

// start the thread
$tr->start(); // creates a new context, instantiates SomeClass, then invokes SomeClass::run()

$results = [];

while ($tr->alive()) { // whilst the thread is still alive
    if ($tr->hasData()) { // if it has data
        $results[] = $tr->getData(); // dequeue an item from the shared memory space
    }
}

$tr->join(); // join the thread to destroy the execution context
```

The message passing approach ...

With the above in mind, the serialisation points are:
 - The arguments to the `ThreadRef` constructor
 - The values pushed to the shared memory queue
 - The values sent via message passing
