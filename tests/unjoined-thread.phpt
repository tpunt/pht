--TEST--
Testing unjoined thread logic
--FILE--
<?php

$thread = new FileThread('unjoined-thread-helper.php');
$thread->start();

$thread = new Thread();
$thread->addFunctionTask(function () {var_dump(1);});
$thread->start();
--EXPECT--
int(1)
