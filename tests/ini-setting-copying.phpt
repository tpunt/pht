--TEST--
Testing the correct copying of ini settings
--FILE--
<?php

use pht\Thread;

ini_set('error_reporting', 32767);
var_dump(ini_get('error_reporting'));
ini_set('error_reporting', 1);
var_dump(ini_get('error_reporting'));

$t = new Thread();
$t->addFunctionTask(function () {var_dump(ini_get('error_reporting'));});
$t->start();
$t->join();
--EXPECT--
string(5) "32767"
string(1) "1"
string(1) "1"
