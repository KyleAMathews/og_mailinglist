#!/usr/bin/php
<?php

require_once("/usr/share/php/Mail/mimeDecode.php");
require_once("phpmailer/class.phpmailer.php");
// Require the QueryPath core. 
require_once('QueryPath/QueryPath.php');


###############################################################################
###   This script is called from Exim4.  It is a "pipe" type of 
###   transport that takes an email and processes it.  It also requires
###   a router file: og_mailinglist_exim4_router.php
###
###   Written by Conan Albrecht   March 2009
###
###   Here's the code that needs to go into Exim4's configuration:
###   (note you need to customize the path in the command line)
###
###   drupal_og_mailinglist:
###     driver = pipe
###     path = "/bin:/usr/bin:/usr/local/bin"
###     command = /var/og_mailinglist/og_mailinglist_exim4_transport.php $local_part
###     user = mail
###     group = mail
###     return_path_add
###     delivery_date_add
###     envelope_to_add
###     log_output
###     return_fail_output
###
###   To test this script from the command line, run the following:
###
###       exim4 -t < email.txt
###
###       where email.txt is an email saved to a file.
###

try {
  # boostrap drupal
  # set up the drupal directory -- very important 
  $DRUPAL_DIR = '/home/kyle/workspace/www/edully';
  
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
  drupal_bootstrap(DRUPAL_BOOTSTRAP_FULL);
  
  # restore error reporting to its normal setting
  error_reporting(E_ALL);

  # set command line arguments (sent by the exim4 transport) to variables we can read
  $mail_username = $argv[1];

  # grab the email message from stdin, then parse the parts
  # we only use the text/plain part right now
  $fd = fopen("php://stdin", "r");
  $email_text = "";
  while (!feof($fd)) {
    $email_text .= fread($fd, 1024);
  }
  
  // Detect email character set.
  $char_set = _og_mailinglist_detect_email_char_set($email_text);
  
  $email = array();
  
  // Extract all the needed info from the email into a simple array.
  $email = _og_mailinglist_parse_email($email_text, $char_set);  
}
catch (Exception $e) {
  echo $e;
}

function _og_mailinglist_parse_email($email_text, $char_set) {
  // Initialize $email array.
  $email = array();
  
  $params['include_bodies'] = true; 
  $params['decode_bodies'] = true; 
  $params['decode_headers'] = true; 
  $params['input'] = $email_text;
  //$params['debug'] = true;

  // do the decode
  $structure = Mail_mimeDecode::decode($params);
  
  //print_r($structure);
  $structure = _og_mailinglist_rewrite_headers($structure);
  $structure = _og_mailinglist_add_footer($structure);
  $new_email = _og_mailinglist_encode_email(array($structure));
  print_r($new_email);
  exit();
}

// Keep mime-version, date, subject, from, to, and content-type
function _og_mailinglist_rewrite_headers($structure) {
  $headers = $structure->headers;
  $new_headers = array();
  $new_headers['mime-version'] = $headers['mime-version'];
  $new_headers['date'] = $headers['date'];
  $new_headers['subject'] = $headers['subject'];
  $new_headers['from'] = $headers['from'];
  $new_headers['to'] = 'mathews.kyle@gmail.com'; //$headers['to'];
  //$new_headers['cc'] = $headers['cc'];
  $new_headers['content-type'] = $headers['content-type'];
  $new_headers['content-transfer-encoding'] =  $headers['content-transfer-encoding'];
  
  $structure->headers = $new_headers;
  
  return $structure;
}

function _og_mailinglist_add_footer($structure) {
  $headers = $structure->headers;
  // If message is 7/8bit text/plain and uses us-ascii charecter set, just 
  // append the footer.
  if (preg_match('/^text\/plain/i', $headers['content-type']) &&
      isset($structure->body)) {
     $structure->body .= "\n\n____________\nKyle is super cool\nWow\nThe end";
  }
  // If message is already multipart, just append new part w/ footer to end
  // /^multipart\/(mixed|related)/i
  else if (preg_match('/^multipart\/(mixed|related)/i', $headers['content-type']) 
            && isset($structure->parts)) {
    $structure->parts[] = (object) array(
    "headers" => array (
      "content-type" => 'text/plain; charset="us-ascii"',
      "mime-version" => '1.0',
      "content-transfer-encoding" => '7bit',
      "content-disposition" => 'inline',
    ),  
      "ctype_primary" => 'text',
      "ctype_secondary" => 'plain',
      "ctype_parameters" => array (
        "charset" => 'us-ascii',
      ),

    "disposition" => 'inline',
    "body" => '_______________________________________________
This is a footer
for emails that
already are in multipart/mixed',
    );
  }
  else {  
    // Else, move existing fields into new MIME entity surrounded by new multipart
    // and append footer field to end.
    $structure->headers['mime-version'] = "1.0";
    $boundary = "Drupal-Mailing-List--" . rand(100000000, 9999999999999);
    
    // Copy email, remove headers from copy, rewrite the content-type, add
    // email copy as parts.
    $content_type = $structure->headers['content-type'];
    $str_clone = clone $structure;
    $str_clone->headers = array('content-type' => $content_type);
    
    $structure->headers['content-type'] = "multipart/mixed; boundary=\"" .
        $boundary . "\"";
    $structure->ctype_primary = "multipart";
    $structure->ctype_secondary = "mixed";
    $structure->ctype_parameters = array('boundary' => $boundary);
    $structure->parts = array($str_clone);
       $structure->parts[] = (object) array(
      "headers" => array (
        "content-type" => 'text/plain; charset="us-ascii"',
        "mime-version" => '1.0',
        "content-transfer-encoding" => '7bit',
        "content-disposition" => 'inline',
      ),  
        "ctype_primary" => 'text',
        "ctype_secondary" => 'plain',
        "ctype_parameters" => array (
          "charset" => 'us-ascii',
        ),
  
      "disposition" => 'inline',
      "body" => '_______________________________________________
This is a footer
for emails that
had to be converted
to multipart/mixed',
      );
  }
  
  //print_r($str_clone);
  //print_r($headers);
  print_r($structure);
  return $structure;
}

// Turn structure back into a plain text email using recursion.
function _og_mailinglist_encode_email($structure, $boundary = "", $email = "") {
  foreach($structure as $part) {
    //echo "\n\n\n\n===========================NEW PART======================\n\n";
    //print_r($part);
    
    if (empty($boundary)) {
      $boundary = $part->ctype_parameters['boundary'];
    }
    
    //$email .= "boundary: " . $boundary . "\n";
    
    
    
    if (isset($part->parts)) {
      
      $email .= _og_mailinglist_encode_email_headers($part->headers) . "\n";
      $email .= "--" . $part->ctype_parameters['boundary'] . "\n";
      $email = _og_mailinglist_encode_email($part->parts, $part->ctype_parameters['boundary'], $email);
      $email .= "--" . $part->ctype_parameters['boundary'] . "--\n";
    }
    else {
      // Non-multipart emails don't have boundaries
      if ($boundary) {
        $last_line = array_pop(explode("\n", trim($email)));
        if (strcmp(trim($last_line), trim("--" . $boundary)) != 0) {
          $email .= "--" . $boundary . "\n";  
        } 
      }
      
      $email .= _og_mailinglist_encode_email_headers($part->headers) . "\n";
      //$email .= "encoding: " . mb_detect_encoding($part->body);
      // Encode the body if necessary
      if ($part->headers['content-transfer-encoding'] == "base64") {
        $email .= wordwrap(base64_encode($part->body), 76, "\n", true);
        $email .= "\n";
      }
      else {
        $email .= $part->body . "\n";
      }
    }
    
  }
  return $email;
}

function _og_mailinglist_encode_email_headers($array) {
  $header = "";
  foreach ($array as $key => $value) {
    // We remove quoted-printable as content-transfer-encoding
    // because mime_decode decodes that and PHP doesn't know how to reencode it.
    if ($value && $value !== "quoted-printable") { 
      $header .= capitalizeWords($key, " -") . ": " . $value . "\n";  
    }
  }
  
  return $header;
}

/**
 * make a recursive copy of an array 
 *
 * @param array $aSource
 * @return array    copy of source array
 */
function array_copy ($aSource) {
    // check if input is really an array
    if (!is_array($aSource)) {
        throw new Exception("Input is not an Array");
    }
    
    // initialize return array
    $aRetAr = array();
    
    // get array keys
    $aKeys = array_keys($aSource);
    // get array values
    $aVals = array_values($aSource);
    
    // loop through array and assign keys+values to new return array
    for ($x=0;$x<count($aKeys);$x++) {
        // clone if object
        if (is_object($aVals[$x])) {
            $aRetAr[$aKeys[$x]]=clone $aVals[$x];
        // recursively add array
        } elseif (is_array($aVals[$x])) {
            $aRetAr[$aKeys[$x]]=array_copy ($aVals[$x]);
        // assign just a plain scalar value
        } else {
            $aRetAr[$aKeys[$x]]=$aVals[$x];
        }
    }
    
    return $aRetAr;
}

function _og_mailinglist_detect_email_char_set($email_text) {
  $mail = mailparse_msg_create();
  mailparse_msg_parse($mail, $email_text);
  $struct = mailparse_msg_get_structure($mail); 
  $info = array();
  foreach($struct as $st) { 
    $section = mailparse_msg_get_part($mail, $st); 
    $info = mailparse_msg_get_part_data($section); 
    if ($info["content-type"] == "text/plain") {
      break;
    }
  }
  
  return $info["content-charset"];
}

/**
 * Capitalize all words
 * @param string Data to capitalize
 * @param string Word delimiters
 * @return string Capitalized words
 * Function taken from http://www.php.net/manual/en/function.ucwords.php#95325
 */
function capitalizeWords($words, $charList = null) {
    // Use ucwords if no delimiters are given
    if (!isset($charList)) {
        return ucwords($words);
    }
    
    // Go through all characters
    $capitalizeNext = true;
    
    for ($i = 0, $max = strlen($words); $i < $max; $i++) {
        if (strpos($charList, $words[$i]) !== false) {
            $capitalizeNext = true;
        } else if ($capitalizeNext) {
            $capitalizeNext = false;
            $words[$i] = strtoupper($words[$i]);
        }
    }
    
    return $words;
}