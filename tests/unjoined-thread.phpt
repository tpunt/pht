--TEST--
Testing unjoined thread logic
--FILE--
<?php

use pht\Thread;

$thread = new Thread();
$thread->addFunctionTask(function () {var_dump(1);});
$thread->start();
--EXPECT--
int(1)
