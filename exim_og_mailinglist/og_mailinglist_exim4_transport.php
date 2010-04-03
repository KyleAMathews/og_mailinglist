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
    // Write the shell script then execute it.
    $script .= "EMAIL=$(cat <<EOF\n";
    $script .= trim($raw_email);
    $script .= "\nEOF\n)\n\n";
    $script .= "drush " . $drush_alias . " ogm-post-email \"\$EMAIL\" " . $mail_username;
    $rand_str = rand(0, 100000);
    //echo $rand_str;
    write_string_to_file($script, $rand_str);
    exec('sh /tmp/' . $rand_str, $result);
    //echo "the result:\n";
    //print_r($result);
  }
}


function write_string_to_file($data, $name = "lsjdf") {
  $myFile = "/tmp/" . $name;
  $fh = fopen($myFile, 'w') or die("can't open file");
  ob_start();
  print_r($data);
  $stringData = ob_get_clean();
    
  fwrite($fh, $stringData . "\n");
  fclose($fh);
}
