#!/usr/bin/php
<?php

// Reads in raw email off the STDIN and posts the email using our drush command on the appropriate site.

// Grab the raw email message from stdin.
$fd = fopen("php://stdin", "r");
while (!feof($fd)) {
  $raw_email .= fread($fd, 1024);
}

// Set command line arguments (sent by the exim4 transport) to variables.
$mail_username = $argv[1];
$mail_domain = $argv[2];

// Load sites
require_once('site_post_urls.php');
$sites = og_mailinglist_site_post_urls();

$post_url = "";
foreach ($sites as $domain => $url) {
  if (strtolower($mail_domain) === $domain) {
    $post_url = $url;
  }
}

if (empty($post_url)) {
  echo "Could not match the email domain with a Drupal site";
  exit();
}

$token = md5('Set this at admin/settings/og_mailinglist then paste here.' . $raw_email . $mail_username);

$ch = curl_init();
curl_setopt($ch, CURLOPT_RETURNTRANSFER, FALSE);
curl_setopt($ch, CURLOPT_POST, 1);
curl_setopt($ch, CURLOPT_HEADER, 1);
curl_setopt($ch, CURLOPT_URL, $post_url);

//prepare the field values being posted to the service
$data = array(
  'message' => $raw_email,
  'mail_username' => $mail_username,
  'token' => $token,
);

curl_setopt($ch, CURLOPT_POSTFIELDS, $data);

//make the request
curl_exec($ch);
