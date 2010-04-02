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
 
//echo $mail_username . " " . $mail_domain . "\n"; 
foreach ($sites as $domain => $drush_alias) {
  if (strtolower($mail_domain) === $domain) {
    // This email is to one of our domains. Let's post it.
    // Multiline arguments are a bit tricky -- see http://www.qc4blog.com/?p=589
    exec("EMAIL=$(cat <<EOF\n" . escapeshellarg($raw_email) . "\nEOF\n);
         drush " . $drush_alias . " ogm-post-email \"\$EMAIL\" " . $mail_username,
         $result);
    echo "the result:\n";
    print_r($result);
  }
}