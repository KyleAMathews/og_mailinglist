#!/usr/bin/php
<?php

###############################################################################
###   This script is called from Exim4.  It is a "queryprogram" type of 
###   router that says whether we'll accept an email or not.  It also requires
###   a transport file: mailnode_exim4_transport.php
###
###   Written by Conan Albrecht   March 2009
###
###   Here's the code that needs to go into Exim4's configuration:
###   (note you need to customize the path in the command line)
###
###   drupal_mailnode:
###     driver = queryprogram
###     command = /var/mailnode/mailnode_exim4_router.php $local_part $domain
###     command_user = mail
###     command_group = mail
###     transport = drupal_mailnode 
###

# set command line arguments (sent by the exim4 router) to variables we can read
$mail_username = $argv[1];
$mail_domain = $argv[2];
$sender_username = $argv[3];
$sender_domain = $argv[4];

# If the return path is from Island, drop it. This occurs because all emails
# originating from the system are CC'd to the group@island.byu.edu.  This allows 
# the user to click Reply to go the user, and Reply All to go to the full group.
# If we allow the CC: to route back into the system, it creates an infinite loop.
if (strpos(strtolower($sender_domain), 'island.byu.edu') !== false ) {
  echo "FAIL\n";
  exit(0);
}

# if not in our drupal domain, decline to handle this mail
# note: do we really care?  Perhaps we shouldn't check the domain?
# should we just worry about the username matching a group?
if (strtolower($mail_domain) != "island.byu.edu") {
  echo "DECLINE\n";
  exit(0);
}

# boostrap drupal
require_once('mailnode_exim4_boostrap_database.php');

# if the user field is not to one of our groups, kick out
$sql = "SELECT value FROM {purl} WHERE LOWER(value)= '%s'";
$result = db_result(db_query($sql, strtolower($mail_username))); // Run the query
if (!$result) {
  echo "DECLINE\n";
  exit(0);
}

# if we get here, we have success
echo "ACCEPT\n";