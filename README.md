# The Pht Threading Extension

This extension exposes a new approach to threading in PHP. Everything and anything is still subject to change.

Quick feature list:
 - Classes, functions, and files may be threaded
 - The inter-thread communication (ITC) data structures include: hash table, queue, vector
 - Threads are reusable for any number of tasks

Requirements:
 - A ZTS version of PHP 7.2. The master branch of php-src is not currently compatible

Any Unix-based OS is supported (including OS X), along with Windows. This extension was explicitly tested on OS X (Yosemite), Ubuntu 14.04 (32bit), and Windows Server 2012 (the pthreads-win32 library is needed).

Contents:
 - [The Basics](https://github.com/tpunt/pht#the-basics)
 - [API](https://github.com/tpunt/pht#api)
 - [Quick Examples](https://github.com/tpunt/pht#quick-examples)
   - [Threading Types](https://github.com/tpunt/pht#threading-types)
     - [Class Threading](https://github.com/tpunt/pht#class-threading)
     - [Function Threading](https://github.com/tpunt/pht#function-threading)
     - [File Threading](https://github.com/tpunt/pht#file-threading)
   - [Inter-Thread Communication Data Structures](https://github.com/tpunt/pht#inter-thread-communication-data-structures)
     - [Queue](https://github.com/tpunt/pht#queue)
     - [Vector](https://github.com/tpunt/pht#vector)
     - [Hash Table](https://github.com/tpunt/pht#hash-table)
   - [Atomic Values](https://github.com/tpunt/pht#atomic-values)
     - [Atomic Integer](https://github.com/tpunt/pht#atomic-integer)

## The Basics

This approach to threading abstracts away the thread itself behind a dedicated object (either a `Thread` or `FileThread`).

If we are dealing with a `Thread`, then we add tasks to the thread's internal task queue:
```php
<?php

use pht\{Thread, Runnable};

class Task implements Runnable
{
    public function run() {}
}

$thread = new Thread();

// Thread::addTask(string $className, mixed ...$constructorArgs);
$thread->addTask(Task::class);
// Thread::addFunctionTask(callable $fn, mixed ...$fnArgs);
$thread->addFunctionTask(function () {});

$thread->start();
$thread->join();
```

The class to be threaded will be instantiated inside of the new thread, where it will execute in isolation without being passed around between threads.

If we are dealing with a `FileThread`, then we simply specify the name of the file we would like to thread:
```php
<?php

use pht\FileThread;

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

With this approach to threading, only the given built-in data structures need to be safely passed around between threads.

This means that the serialisation points to be aware of are:
 - The arguments being passed to `Thread::addTask()` and `FileThread::__construct()`
 - The values being placed into the ITC-based data structures

## API

```php
<?php

namespace pht;

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
    public function run(void) void;
}

// internal interface, not implementable by userland PHP classes
interface Threaded
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
    public function size(void) : int;
    // ArrayAccess API is enabled, but the userland interface is not explicitly implemented
}

class Vector implements Threaded
{
    public function __construct([int $size = 0 [, mixed $defaultValue = 0]]);
    public function resize(int $size [, mixed $defaultValue = 0]);
    public function push(mixed $value) : void;
    public function pop(void) : mixed;
    public function shift(void) : mixed;
    public function unshift(mixed $value) : void;
    public function insertAt(mixed $value, int $index) : void;
    public function updateAt(mixed $value, int $index) : void;
    public function deleteAt(int $index) : void;
    public function lock(void) : void;
    public function unlock(void) : void;
    public function size(void) : int;
    // ArrayAccess API is enabled, but the userland interface is not explicitly implemented
}

class AtomicInteger implements Threaded
{
    public function __construct([int $value = 0]);
    public function get(void) : int;
    public function set(int $value) : void;
    public function inc(void) : void;
    public function dec(void) : void;
    public function lock(void) : void;
    public function unlock(void) : void;
}
```

## Quick Examples

This section demonstrates some quick examples of the basic features. For generic examples, see the [examples](https://github.com/tpunt/pht/tree/master/examples) folder instead.

### Threading Types

#### Class Threading

Classes that will be threaded need to implement the `Runnable` interface.

```php
<?php

use pht\{Thread, Runnable};

class Task implements Runnable
{
    private $one;

    public function __construct(int $one)
    {
        $this->one = $one;
    }

    public function run()
    {
        var_dump($this->one);
    }
}

$thread = new Thread();

// Thread::addTask(string $className, mixed ...$ctorArgs);
$thread->addTask(Task::class, 1);

$thread->start();
$thread->join();
```

All `$ctorArgs` being passed into the `Thread::addTask()` method will be serialised.

#### Function Threading

Functions must not refer to `$this` (it will become `null` in the threaded context), and must not import variables from their outer scope (via the `use` statement). They should be completely standalone.

```php
<?php

use pht\Thread;

class Test
{
    public static function run(){var_dump(5);}
    public static function run2(){var_dump(6);}
}

function aFunc(){var_dump(3);}

$thread = new Thread();

// Thread::addFunctionTask(callable $fn, mixed ...$fnArgs);
$thread->addFunctionTask(static function($one) {var_dump($one);}, 1);
$thread->addFunctionTask(function() {var_dump(2);});
$thread->addFunctionTask('aFunc');
$thread->addFunctionTask('array_map', function ($n) {var_dump($n);}, [4]);
$thread->addFunctionTask(['Test', 'run']);
$thread->addFunctionTask([new Test, 'run2']);

$thread->start();
$thread->join();
```

All `$fnArgs` being passed into the `Thread::addFunctionTask()` method will be serialised.

#### File Threading

To pass data to the file being threaded, pass them through the `FileThread` constructor. They will then become available in a special `$_THREAD` superglobals array inside of the threaded file.

```php
<?php

use pht\FileThread;

$fileThread = new FileThread('file.php', 1, 2, 3);

$fileThread->start();
$fileThread->join();
```

`file.php`
```php
<?php

[$one, $two, $three] = $_THREAD;

var_dump($one, $two, $three);
```

All `$globals` being passed into the `FileThread::__construct()` method will be serialised.

### Inter-Thread Communication Data Structures

The inter-thread communication (ITC) data structures enable for a two-way communication style between threads.

They are:
 - safe to pass around between threads
 - reference-counted across threads
 - nestable within one-another (such as using a `Vector` in a `Vector`)

Things to note:
 - cyclic references will leak (due to the simplistic nature of reference counting)
 - all values placed into these data structures will be serialised
 - the mutexes exposed by these data structures are not reentrant

#### Queue

```php
<?php

use pht\{Thread, Queue};

$thread = new Thread();
$queue = new Queue();
$queueItemCount = 5;

$thread->addFunctionTask(function ($queue, $queueItemCount) {
    for ($i = 0; $i < $queueItemCount; ++$i) {
        $queue->lock();
        $queue->push($i);
        $queue->unlock();
    }
}, $queue, $queueItemCount);

$thread->start();

while (true) {
    $queue->lock();

    if ($queue->size() === $queueItemCount) {
        $queue->unlock();
        break;
    }

    $queue->unlock();
}

$thread->join();

// since we are no longer using $queue in multiple threads, we don't need to lock it
while ($queue->size()) {
    var_dump($queue->pop());
}
```

#### Vector

```php
<?php

use pht\{Thread, Vector};

$thread = new Thread();
$vector = new Vector();
$vectorItemCount = 5;

for ($i = 0; $i < $vectorItemCount; ++$i) {
    $thread->addFunctionTask(function ($vector, $i) {
        $vector->lock();
        $vector->push($i);
        $vector->unlock();
    }, $vector, $i);
}

$thread->start();

while (true) {
    $vector->lock();

    if ($vector->size() === $vectorItemCount) {
        $vector->unlock();
        break;
    }

    $vector->unlock();
}

$thread->join();

// since we are no longer using $vector in multiple threads, we don't need to lock it
for ($i = 0; $i < $vectorItemCount; ++$i) {
    var_dump($vector[$i]);
}
```

#### Hash Table

```php
<?php

use pht\{Thread, HashTable};

$thread = new Thread();
$hashTable = new HashTable();
$hashTableItemCount = 5;

for ($i = 0; $i < $hashTableItemCount; ++$i) {
    $thread->addFunctionTask(function ($hashTable, $i) {
        $hashTable->lock();
        $hashTable[chr(ord('a') + $i)] = $i;
        $hashTable->unlock();
    }, $hashTable, $i);
}

$thread->start();

while (true) {
    $hashTable->lock();

    if ($hashTable->size() === $hashTableItemCount) {
        $hashTable->unlock();
        break;
    }

    $hashTable->unlock();
}

$thread->join();

// since we are no longer using $hashTable in multiple threads, we don't need to lock it
for ($i = 0; $i < $hashTableItemCount; ++$i) {
    var_dump($hashTable[chr(ord('a') + $i)]);
}
```

### Atomic Values

Atomic values are classes that wrap simple values. These values are safe to update without acquiring mutex locks, but they also pack with them mutex locks should multiple operations need to be performed together. The mutex locks, for this reason, are reentrant.

#### Atomic Integer

```php
<?php

use pht\{Thread, AtomicInteger};

$thread = new Thread();
$atomicInteger = new AtomicInteger();
$max = 100000;

$thread->addFunctionTask(function ($atomicInteger, $max) {
    for ($i = 0; $i < $max; ++$i) {
        $atomicInteger->inc();
    }
}, $atomicInteger, $max);

$thread->start();

// safe
for ($i = 0; $i < $max; ++$i) {
    $atomicInteger->inc();
}

// safe
while ($atomicInteger->get() !== $max * 2);

// requires mutex locking since we need to perform multiple operations together
$atomicInteger->lock();
$atomicInteger->set($atomicInteger->get() * 2);
$atomicInteger->unlock();

$thread->join();

var_dump($atomicInteger->get()); // int(400000)
```
