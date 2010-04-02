#!/usr/bin/php
<?php

###############################################################################
###   This script is called from Exim4.  It is a "queryprogram" type of 
###   router that says whether we'll accept an email or not.  It also requires
###   a transport file: og_mailinglist_exim4_transport.php
###
###

// Load site aliases
require_once('site_aliases.php');
$sites = og_mailinglist_site_aliases();

# set command line arguments (sent by the exim4 router) to variables we can read
$mail_username = $argv[1];
$mail_domain = $argv[2];
$sender_username = $argv[3];
$sender_domain = $argv[4];

# If the return path is from this server, drop it. This occurs because all emails
# originating from the system are CC'd to the group@yoursite.com.  This allows 
# the user to click Reply to go the user, and Reply All to go to the full group.
# If we allow the CC: to route back into the system, it creates an infinite loop.
$fail = false;
foreach ($sites as $domain => $drush_alias) {
  if (strpos(strtolower($sender_domain), $domain) !== false ) {
    $fail = true;
  } 
}
if ($fail) {
  echo "FAIL\n";
  exit(0);
}

# If not in one of our site's domain, decline to handle this mail
$decline = true;
foreach ($sites as $domain => $drush_alias) {
  if (strtolower($mail_domain) === $domain) {
    // This email is to one of our domains.
    $decline = false;
    
    // Let's check now if the email is directed at one of our groups.
    // call drush command that returns true false,
    // if true continue, else echo decline and exit
    exec("drush " . $drush_alias . " ogm-group-exists " . $mail_username, $result);
    
    if (!$result[0]) {
      echo "DECLINE\n";
      exit(0);
    }
    
  }
}
if ($decline) {
  echo "DECLINE\n";
  exit(0);
}


// If we get here, we have success
echo "ACCEPT\n";
