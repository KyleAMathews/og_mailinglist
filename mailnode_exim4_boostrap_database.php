<?php

############################################################
###  This file boostraps Drupal so it can be run
###  from a command-line-driven script.  Include this file
###  at the top of your scrip.
###
###  Written by Conan Albrecht with input from various web
###  site tutorials.   March 2009.


# set up the drupal directory -- very important 
$DRUPAL_DIR = '/var/www/island_prod/';

# set some server variables so Drupal doesn't freak out
$_SERVER['SCRIPT_NAME'] = '/script.php';
$_SERVER['SCRIPT_FILENAME'] = '/script.php';
$_SERVER['HTTP_HOST'] = 'example.com';
$_SERVER['REMOTE_ADDR'] = '127.0.0.1';
$_SERVER['REQUEST_METHOD'] = 'POST';
 
# act as the first user
global $user;
$user->uid = 1;
 
# gain access to drupal
chdir($DRUPAL_DIR);  # drupal assumes this is the root dir
error_reporting(E_ERROR | E_PARSE); # drupal throws a few warnings, so suppress these
require_once('./includes/bootstrap.inc');
drupal_bootstrap(DRUPAL_BOOTSTRAP_DATABASE);

# restore error reporting to its normal setting
error_reporting(E_ALL);