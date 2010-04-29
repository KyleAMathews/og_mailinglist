#!/usr/bin/php
<?php

// Reads in raw email off the STDIN and posts the email using our drush command on the appropriate site.

// Grab the raw email message from stdin.
$fd = fopen("php://stdin", "r");
while (!feof($fd)) {
  $raw_email .= fread($fd, 1024);
}

// Load site aliases
require_once('site_aliases.php');
$sites = og_mailinglist_site_aliases();

// Set command line arguments (sent by the exim4 transport) to variables.
$mail_username = $argv[1];
$mail_domain = $argv[2];
 
foreach ($sites as $domain => $drush_alias) {
  if (strtolower($mail_domain) === $domain) {
    // Save the email to file. We would just pass the email directly through
    // bash but as it turns out, there's a size limit to how big bash arguments
    // can be and emails with large attachment's exceed that limit.
    $rand_filename = "/tmp/" . rand(1000, 10000000);
    file_put_contents($rand_filename, $raw_email . "\n");
    exec("drush " . $drush_alias . " ogm-post-email " . $rand_filename . " " . $mail_username,
         $result);
  }
}