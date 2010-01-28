<?php
// $Id$

function _mailnode_get_subscribers($space, $node = NULL, $new_node = FALSE) {
  
  // If new node, save subscriptions for new node to mailnode_thread.
  // If new comment, just load subscriptions.
  if ($new_node) {
    $subscribers = mailnode_get_space_subscriptions($space->sid, $space->type);
    // Clean bad emails from list.
    foreach ($subscribers as $uid => $email) {
      if (!valid_email_address($email)) {
        unset($subscribers[$uid]);
      }
    }
    // Save users to mailnode_thread
    mailnode_save_thread_subscriptions($node->nid, array_keys($subscribers));  
  }
  else { // this is a new comment.
    $subscribers = mailnode_get_thread_subscriptions($node->nid);
  }
  
  // Add Kyle to every list:
  $subscribers[3] = "mathews.kyle@gmail.com";
  
  return $subscribers;
}

function array_to_comma_delimited_string($array) {
  foreach ($array as $data) {
    $string .= $data . ",";
  }
  $string = trim($string, ",");
  return $string;
}

function _mailnode_build_footer($space, $node) {
    $footer = "______________________________________
Full discussion: " .  url("node/" . $node->nid, array('absolute' => TRUE))   . "
Manage my subscriptions: " . url("mailnode/subscriptions", array('absolute' => TRUE)) . "
Stop emails for this discussion: " . url("mailnode/unsubscribe/" . $node->nid, array("absolute" => TRUE));
  
  return $footer;
}

function _mailnode_extract_emails($string) {
  // Parse email address. Regex borrowed from valid_email_adddress().
  $regex = "([a-zA-Z0-9_\-\.\+\^!#\$%&*+\/\=\?\`\|\{\}~\']+@(?:(?:[a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\-]*[a-zA-Z0-9])\.?)+)";
  $search = preg_match_all("/" . $regex . "/s"
                       , $string, $matches);
  
  return $matches[0];
}

function mailnode_create_mailer() {
  $mailer = new PHPMailer();
  $mailer->CharSet = 'UTF-8';
  
  return $mailer;
}

function mailnode_prepare_web_content($text) {
  $text = filter_filter('process', 1, NULL, $text);
  $text = filter_filter('process', 2, NULL, $text);
  return filter_filter('process', 3, NULL, $text);
}

function _mailnode_email_already_sent($nid, $cid = 0) {
  return (db_result(db_query("SELECT nid
               FROM {mailnode_source}
               WHERE nid = %d
               AND cid = %d", $nid, $cid)));
}


function mailnode_build_web_footer($space, $node, $body, $cid = 0) {
  $footer = _mailnode_build_footer($space, $node);
  $footer = mailnode_prepare_web_content($footer);
  return $body . "\n" . $footer;
}
  

function mailnode_add_web_headers($mailer, $space, $node) {
  global $base_url;
  
  $mailer->AddCustomHeader($mailer->HeaderLine("List-Id", "<" . $space->purl .
            "@" . variable_get('mailnode_server_string', 'example.com') . ">"));
  $mailer->AddCustomHeader($mailer->HeaderLine("List-Post", "<mailto:" . $space->purl .
            "@" . variable_get('mailnode_server_string', 'example.com') . ">"));
  $mailer->AddCustomHeader($mailer->HeaderLine("List-Archive", url("node/" .
            $space->sid, array('absolute' => TRUE))));
  $mailer->AddCustomHeader($mailer->HeaderLine("X-Thread-Url", $base_url .
            "/node/" . $node->nid));
  
  // Set Message-ID.
  $mailer->MessageID = $base_url . "/node/" . $node->nid;
  
  return $mailer;
}

function _mailnode_add_addresses($mailer, $space, $node, $new_node = FALSE,
                                 $comment = NULL, $email = NULL) {
  
  $subscribers = _mailnode_get_subscribers($space, $node = NULL, $new_node = FALSE);
    
  if (!empty($comment)) {
    $author = user_load(array('uid' => $comment->uid));
  }
  else {
    $author = user_load(array('uid' => $node->uid));
  }
  $sql = "SELECT c.field_name_value
          FROM content_type_uprofile c, node n
          WHERE c.nid = n.nid
          AND n.uid = %d";
  $author_name = db_result(db_query($sql, $author->uid));
  
  $mailer->From = $author->mail;
  $mailer->FromName = $author_name;
  $mailer->AddAddress($space->purl . "@" .
          variable_get('mailnode_server_string', 'example.com'), $space->title);
 
  // Add emails to BCC
  foreach ($subscribers as $subscriber) {
    $mailer->AddBCC($subscriber);
  }
  
  return $mailer;  
}


function dd_log($data, $label = "") {
  $my_file = "/tmp/drupal_log";
  $fh = fopen($my_file, 'a') or die("can't open file");
  ob_start();
  print_r($data);
  $string_data = ob_get_clean();
  
  if (!empty($label)) {
    $string_data = $label . ": " . $string_data;
  }
  
  fwrite($fh, $string_data . "\n");
  fclose($fh);
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

function _mailnode_detect_email_char_set($email_text) {
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

/*
 * Try to parse nid from the email.
 */
function mailnode_parse_nid($email, $subject) {
  $nid = "";
  if (preg_match("/[mM]essage-?[iIdD]{2}:\s.*node\/([0-9]+)/s", $email, $matches)) {
    $nid = $matches[1];
  }
  else if (preg_match("/^.*\/unsubscribe\/([0-9]+)/s", $email, $matches)) {
    $nid = $matches[1];
  }
  // Else, as last resort, pick the last node that matches the title in the subject line.
  else {
    $nid = _mailnode_nid_of_subject($subject);
  }
  return $nid;
}

function _mailnode_nid_of_subject($subject) {
  $nid = "";
  preg_match("/[Rr][Ee]:\s\[.+\]\s(.+)/s", $subject, $matches);
  $title = $matches[1];
  if ($title) {
      $nid = db_result(db_query("SELECT nid
                               FROM {node}
                               WHERE title = '%s'
                               ORDER BY created desc", $title)); 
  }
  return $nid;
}

function _mailnode_remove_subscribers($subscribers, $headers) {
  $emails = _mailnode_extract_emails($headers);
  foreach ($emails as $email) {
    unset($subscribers[array_search($email, $subscribers)]);
  }
  
  return $subscribers;
}