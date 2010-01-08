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
  
  //  // If node/comment written on the website.
  //  if (!empty($comment)) {
  //    $author = user_load(array('uid' => $comment->uid));
  //  }
  //  else {
  //    $author = user_load(array('uid' => $node->uid));
  //  }
  //  $sql = "SELECT c.field_name_value
  //          FROM content_type_uprofile c, node n
  //          WHERE c.nid = n.nid
  //          AND n.uid = %d";
  //  $author_name = db_result(db_query($sql, $author->uid));
  //  
  //  $mailer->From = $author->mail;
  //  $mailer->FromName = $author_name;
  //  $mailer->AddAddress($space->purl . "@island.byu.edu", $space->title);
  //
  //// Add emails to BCC
  //foreach ($subscribers as $subscriber) {
  //  $mailer->AddBCC($subscriber);
  //}
  
  return $subscribers;
}

function array_to_comma_delimited_string($array) {
  dd_log($array);
  foreach ($array as $data) {
    $string .= $data . ",";
  }
  $string = trim($string, ",");
  return $string;
}

function _mailnode_build_footer($space, $node, $messageid) {
    $footer = "______________________________________
Full discussion: " .  url("node/" . $node->nid, array('absolute' => TRUE))   . "
Manage my subscriptions: https://island.byu.edu/mailnode/subscriptions
Stop emails for this discussion: " . url("mailnode/unsubscribe/" . $node->nid, array("absolute" => TRUE))
. "\n"
. $messageid;
  
  return $footer;
}

function _mailnode_extract_email($email) {
  // Parse email address. Regex borrowed from valid_email_adddress().
  $regex = "([a-zA-Z0-9_\-\.\+\^!#\$%&*+\/\=\?\`\|\{\}~\']+@(?:(?:[a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\-]*[a-zA-Z0-9])\.?)+)";
  $search = preg_match("/" . $regex . "/s"
                       , $email, $matches);
  
  $address = $matches[1];

  // Query for name (it'd be better perhaps to parse out name as well...
  // but regex is hurting my brain this morning. Maybe later).
  $sql = "SELECT c.field_name_value
          FROM content_type_uprofile c, node n, users u
          WHERE c.nid = n.nid
          AND u.uid = n.uid
          AND u.mail = '%s'";
  
  $name = "";
  $name = db_result(db_query($sql, $address));
  
  return array('name' => $name, 'address' => $address);
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


function mailnode_build_web_footer($space, $node, $body, $messageid, $cid = 0) {
  $footer = _mailnode_build_footer($space, $node, $messageid);
  $footer = mailnode_prepare_web_content($footer);
  return $body . "\n" . $footer;
}
  

function mailnode_add_headers($mailer, $messageid, $space) {
  $mailer->AddCustomHeader($mailer->HeaderLine("Message-ID", "<" . $messageid . ">"));
  $mailer->AddCustomHeader($mailer->HeaderLine("List-Id", "<" . $space->purl . "@island.byu.edu>"));
  $mailer->AddCustomHeader($mailer->HeaderLine("List-Post", "<mailto:" . $space->purl . "@island.byu.edu>"));
  $mailer->AddCustomHeader($mailer->HeaderLine("List-Archive", url("node/" . $space->sid, array('absolute' => TRUE))));
  
  return $mailer;
}

function _mailnode_add_addresses($mailer, $space, $node, $new_node = FALSE,
                                 $comment = NULL, $email = NULL) {
  
  // If new node, save subscriptions for new node to mailnode_thread.
  // If new comment, just load subscriptions.
  if ($new_node) {
    $subscribers = mailnode_get_space_subscriptions($space->sid, $space->type);
    // Clean bad emails from list.
    
    
    
    
    foreach ($subscribers as $uid => $mail) {
      if (!valid_email_address($mail)) {
        dd_log($mail);
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
  $mailer->AddAddress($space->purl . "@island.byu.edu", $space->title);
 
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

