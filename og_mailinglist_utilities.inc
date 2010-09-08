<?php
// $Id$

function _og_mailinglist_get_subscribers($node, $new_node = FALSE) {
  $group_node = _og_mailinglist_load_group($node);
  // If new node, save subscriptions for new node to og_mailinglist_thread.
  // If new comment, just load subscriptions.
  if ($new_node) {
    og_mailinglist_save_group_thread_subscriptions($group_node->nid, $node->nid);
    $subscribers = og_mailinglist_get_thread_subscriptions($node->nid);
  }
  else { // this is a new comment.
    $subscribers = og_mailinglist_get_thread_subscriptions($node->nid);
  }
  
  if (is_numeric(variable_get('og_mailinglist_test_email_address', ''))) {
    $uid = variable_get('og_mailinglist_test_email_address', '');
    $email = db_result(db_query("SELECT mail FROM {users} WHERE uid = %d", $uid));
    $uids[$uid] = $email;
  }
  
  return $subscribers;
}

function array_to_comma_delimited_string($array) {
  foreach ($array as $data) {
    $string .= $data . ",";
  }
  $string = trim($string, ",");
  return $string;
}

function _og_mailinglist_build_footer($node) {
    $footer = "--
Full post: " .  url("node/" . $node->nid, array('absolute' => TRUE))   . "
Manage my subscriptions: " . url("og_mailinglist/subscriptions", array('absolute' => TRUE)) . "
Stop emails for this post: " . url("og_mailinglist/unsubscribe/" . $node->nid, array("absolute" => TRUE));
  
  return $footer;
}

function og_mailinglist_prepare_web_content($text) {
  return check_markup($text);
}

 function _og_mailinglist_email_already_sent($nid, $cid = 0) {
  return (db_result(db_query("SELECT nid
               FROM {og_mailinglist_source}
               WHERE nid = %d
               AND cid = %d", $nid, $cid)));
}


function og_mailinglist_build_web_footer($node, $body, $cid = 0) {
  $footer = _og_mailinglist_build_footer($node);
  $footer = og_mailinglist_prepare_web_content($footer);
  preg_match("/<p>(.+)<\/p>/s", $footer, $match);
  $footer = $match[1] . "<br />";

  return $body . "\n" . $footer;
}
  

function og_mailinglist_add_web_headers($mailer, $node, $comment = 0, $in_reply_to = 0, $references = 0) {
  $group_node = _og_mailinglist_load_group($node);
  global $base_url;
  
  $mailer->AddCustomHeader($mailer->HeaderLine("List-Id", "<" . $group_node->ogm_email .
            "@" . variable_get('og_mailinglist_server_string', 'example.com') . ">"));
  $mailer->AddCustomHeader($mailer->HeaderLine("List-Post", "<mailto:" . $group_node->ogm_email .
            "@" . variable_get('og_mailinglist_server_string', 'example.com') . ">"));
  $mailer->AddCustomHeader($mailer->HeaderLine("List-Archive", url("node/" .
            $group_node->nid, array('absolute' => TRUE))));
  $mailer->AddCustomHeader($mailer->HeaderLine("X-Thread-Url", $base_url .
            "/node/" . $node->nid));
  $mailer->AddCustomHeader($mailer->HeaderLine("X-BeenThere", $group_node->ogm_email .
            "@" . variable_get('og_mailinglist_server_string', 'example.com')));

  // Add in-reply-to / references
  if (!empty($in_reply_to)) {
    $mailer->AddCustomHeader($mailer->HeaderLine("In-Reply-To", $in_reply_to));
  }
  if (!empty($references)) {
    $mailer->AddCustomHeader($mailer->HeaderLine("References", $references));
  }

  // Add a reply-to header directed to the group if enabled.
  if (variable_get('og_mailinglist_reply_to_group', FALSE)) {
    $mailer->AddCustomHeader($mailer->HeaderLine("reply-to", "<" . $group_node->ogm_email .
            "@" . variable_get('og_mailinglist_server_string', 'example.com') . ">"));
  }

  // Set Message-ID.
  $comment_id = (!empty($comment)) ? $comment->cid : 0;
  $mailer->MessageID = "<node-" . $node->nid . "-comment-" . $comment_id . "@" . variable_get('og_mailinglist_server_string', 'example.com') . ">";
  
  return $mailer;
}

function _og_mailinglist_add_addresses($mailer, $node, $new_node = FALSE,
                                 $comment = NULL, $email = NULL) {
  $group_node = _og_mailinglist_load_group($node);
  $subscribers = _og_mailinglist_get_subscribers($node, $new_node);
    
  if (!empty($comment)) {
    $author = user_load(array('uid' => $comment->uid));
  }
  else {
    $author = user_load(array('uid' => $node->uid));
  }
  
  if (isset($author->realname)) {
    $author_name = $author->realname; 
  }
  else {
    $author_name = $author->name; 
  }
  
  if ($author->uid == 0) {
    $author->mail = "anonymous@" . variable_get('og_mailinglist_server_string', 'example.com');
    $author_name = "anonymous";
  }
  try {
    $mailer->From = $author->mail;
    $mailer->FromName = $author_name;
    $mailer->AddAddress($group_node->ogm_email . "@" .
            variable_get('og_mailinglist_server_string', 'example.com'), $group_node->title);

    // Add emails to BCC
    foreach ($subscribers as $subscriber) {
      $mailer->AddBCC($subscriber);
    }
  } catch (phpmailerException $e) {
       watchdog('og_mailinglist', t('PHPMailer error: !error', array('!error' => $e->errorMessage())), NULL, WATCHDOG_ERROR);
  } catch (Exception $e) {
       watchdog('og_mailinglist', $e->getMessage(), NULL, WATCHDOG_ERROR);
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

/*
 * Use every possible method to figure out the group nid of a node.
 */
function _og_mailinglist_load_group($node) {
  if (!empty($node->og_groups_both)) {
    return node_load(array('nid' => current(array_keys($node->og_groups_both))));
  }
  elseif (!empty($node->og_groups)) {
    return node_load(array('nid' => current(array_keys($node->og_groups))));
  }
  elseif (!empty($node->og_initial_groups)) {
    return node_load(array('nid' => current(array_keys($node->og_initial_groups))));
  }
  else {
    $group_node = node_load(array('nid' => db_result(db_query("SELECT group_nid
                                              FROM {og_ancestry}
                                              WHERE nid = %d", $node->nid))));
    if (!empty($group_node)) {
      return $group_node;
    }
    else {
      return null;
    }
  }
}
/**
 * Capitalize all words
 * @param string Data to capitalize
 * @param string Word delimiters
 * @return string Capitalized words
 * Function taken from http://www.php.net/manual/en/function.ucwords.php#95325
 */
function capitalize_headers($words, $charList = null) {
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

/**
 * Loads PHPMailer library if it's not loaded already.
 *
 * @return
 *  TRUE if the PHPMailer library is loaded, FALSE otherwise.
 */
function og_mailinglist_phpmailer_load_library() {
  if (!class_exists('PHPMailer')) {
    // First, try using libraries module.
    if (module_exists('libraries')) {
      // Let's see if PHPMailer is really available from libraries.
      $phpmailer_library = './'. libraries_get_path('PHPMailer') .'/class.phpmailer.php';
      if (file_exists($phpmailer_library)) {
        include_once $phpmailer_library;
      }
    }
    // If PHPMailer is not already loaded, then try from module subdirectory.
    if (!class_exists('PHPMailer')) {
      $phpmailer_library = './'. drupal_get_path('module', 'og_mailinglist') .'/PHPMailer/class.phpmailer.php';
      if (file_exists($phpmailer_library)) {
        include_once $phpmailer_library;
      }
    }
  }
  // Tell the caller if PHPMailer class exists.
  return class_exists('PHPMailer');
}

function og_mailinglist_mimeDecode_load_library() {
  if (!class_exists('Mail_mimeDecode')) {
    // First we'll try grabbing the file from a few default pear install locations.
    if (file_exists('Mail/mimeDecode.php')) {
      include_once('Mail/mimeDecode.php');
    }
    if (!class_exists('Mail_mimeDecode')) {
      if (file_exists('/usr/share/php/Mail/mimeDecode.php')) {
        include_once('/usr/share/php/Mail/mimeDecode.php');
      }
    }
    if (!class_exists('Mail_mimeDecode')) {
      if (file_exists('/usr/share/pear/Mail/mimeDecode.php')) {
        include_once('/usr/share/pear/Mail/mimeDecode.php');
      }
    }
    // If that didn't work, let's try using the libraries module.
    if (!class_exists('Mail_mimeDecode')) {
      if (module_exists('libraries')) {
        // Let's see if PHPMailer is really available from libraries.
        $Mail_mimeDecode_library = './'. libraries_get_path('mimeDecode') .'/mimeDecode.php';
        if (file_exists($Mail_mimeDecode_library)) {
          include_once $Mail_mimeDecode_library;
        }
      }
    }
    // If PHPMailer is not already loaded, then try from module subdirectory.
    if (!class_exists('Mail_mimeDecode')) {
      $Mail_mimeDecode_library = './'. drupal_get_path('module', 'og_mailinglist') .'/mimeDecode/mimeDecode.php';
      if (file_exists($Mail_mimeDecode_library)) {
        include_once $Mail_mimeDecode_library;
      }
    }

  }
  // Tell the caller if PHPMailer class exists.
  return class_exists('Mail_mimeDecode');
}
