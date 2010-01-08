<?php
// $Id$

require_once('mailnode_api.inc');

// Keep mime-version, date, subject, from, to, and content-type
function _mailnode_rewrite_headers($email, $node, $space, $messageid,
                                   $new_node = FALSE) {
  $headers = $email['structure']->headers;
  $new_headers = array();
  $new_headers['mime-version'] = $headers['mime-version'];
  $new_headers['date'] = $headers['date'];
  if ($new_node) {
    $new_headers['subject'] = "[" . $space->purl . "] " . $node->title;  
  }
  else {
    $new_headers['subject'] = $headers['subject'];
  }
  
  $new_headers['from'] = $headers['from'];
  $new_headers['to'] = $headers['to'];
  $new_headers['cc'] = $headers['cc'];
  $new_headers['bcc'] = array_to_comma_delimited_string(_mailnode_get_subscribers($space, $node, TRUE));
  $new_headers['content-type'] = $headers['content-type'];
  $new_headers['content-transfer-encoding'] =  $headers['content-transfer-encoding'];
  
  // Add list headers.
  $new_headers['Message-ID'] = "<" . $messageid . ">";
  $new_headers['List-Id'] = "<" . $space->purl . "@island.byu.edu>";
  $new_headers['List-Post'] = "<mailto:" . $space->purl . "@island.byu.edu>";
  $new_headers['List-Archive'] = url("node/" . $space->sid, array('absolute' => TRUE));
  
  $email['structure']->headers = $new_headers;
  
  dd_log($new_headers);
  
  return $email;
}

function _mailnode_add_footer($email, $footer) {
  $headers = $email['structure']->headers;
  $structure = $email['structure'];
  
  // If message is 7/8bit text/plain and uses us-ascii charecter set, just 
  // append the footer.
  if (preg_match('/^text\/plain/i', $headers['content-type']) &&
      isset($structure->body)) {
     $structure->body .= "\n" . $footer;
  }
  // If message is already multipart, just append new part w/ footer to end
  // /^multipart\/(mixed|related)/i
  elseif (preg_match('/^multipart\/(mixed|related)/i', $headers['content-type']) 
            && isset($structure->parts)) {
    $structure->parts[] = (object) array(
    "headers" => array(
      "content-type" => 'text/plain; charset="us-ascii"',
      "mime-version" => '1.0',
      "content-transfer-encoding" => '7bit',
      "content-disposition" => 'inline',
    ),  
      "ctype_primary" => 'text',
      "ctype_secondary" => 'plain',
      "ctype_parameters" => array(
        "charset" => 'us-ascii',
      ),

    "disposition" => 'inline',
    "body" => $footer,
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
      "headers" => array(
        "content-type" => 'text/plain; charset="us-ascii"',
        "mime-version" => '1.0',
        "content-transfer-encoding" => '7bit',
        "content-disposition" => 'inline',
      ),  
        "ctype_primary" => 'text',
        "ctype_secondary" => 'plain',
        "ctype_parameters" => array(
          "charset" => 'us-ascii',
        ),
  
      "disposition" => 'inline',
      "body" => $footer,
      );
  }
  
  //print_r($str_clone);
  //print_r($headers);
  //print_r($structure);
  dd_log($structure);
  $email['structure'] = $structure;
  return $email;
}

function _mailnode_send_raw_email($email_text) {
  $rand_str = rand(1000, 10000);
  write_string_to_file($email_text, $rand_str);
  system("/usr/sbin/exim4 -t < /tmp/" . $rand_str);
}