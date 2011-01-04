<?php
// $Id$

require_once('og_mailinglist_utilities.inc');
require_once('og_mailinglist_api.inc');

function og_mailinglist_send_comment_email($comment, $node) {
  // Check if comment arrived via email so already sent.
  if (_og_mailinglist_email_already_sent($comment->nid, $comment->cid)) {
    return;
  }
  
  $group_node = _og_mailinglist_load_group($node);

  $mailer = og_mailinglist_create_mailer();
  
  // Add custom headers.
  $parent_message_id = _og_mailinglist_get_messageid_from_nid($node->nid);
  // If the comment is in reply to another comment, set the in-reply-to header
  // to that comment's message-id, else to the parent post's message-id.
  $comment_pid = db_result(db_query("SELECT pid 
                                      FROM {comments}
                                      WHERE cid = %d", $comment->cid));
  if ($comment_pid != 0) {
    $in_reply_to = db_result(db_query("SELECT message_id
                                        FROM {og_mailinglist_source}
                                        WHERE cid = %d", $comment_pid));
    $references = db_result(db_query("SELECT references_header
                                      FROM {og_mailinglist_source}
                                      WHERE cid = %d", $comment_pid)) . " " . $in_reply_to;
  }
  else {
    $in_reply_to = $parent_message_id;
    $references = $parent_message_id; // TODO, this should really also reference each message-id that came before it in order. Easy enough to do w/ a query.
  }
  
  $mailer = og_mailinglist_add_web_headers($mailer, $node, $comment, $in_reply_to, $references);
  
  // Decorate subject.
  $mailer->Subject = "Re: [" . $group_node->ogm_email . "] " . $node->title;

  // Decorate body.
  $body = og_mailinglist_prepare_web_content($comment->comment);
  $body = og_mailinglist_build_web_footer($node, $body);
  
  $mailer->Body = $body;
  $mailer->isHTML = TRUE;
  $mailer->AltBody = drupal_html_to_text($body);

  //// Add attachments if any.
  //foreach ($email['attachments'] as $info) { 
  //  $mailer->AddStringAttachment($info['data'], $info['filename']);
  //}
  
  $mailer = _og_mailinglist_add_addresses($mailer, $node, FALSE,
                                 $comment);
  
  $success = $mailer->Send();
  
  if ($success) {
    og_mailinglist_log_email_sent('web', $node->nid, $comment->cid, $mailer->MessageID, $in_reply_to, $references, $parent_message_id);
  }
  else {
    watchdog('og_mailinglist', "OG_Mailinglist couldn't send a new node email.", NULL,
             WATCHDOG_ERROR);
  }
  
  return;
}

function og_mailinglist_send_node_email($node) {
  // Check if node arrived via email so already sent.
  if (_og_mailinglist_email_already_sent($node->nid)) {
    return;
  }
  
  $group_node = _og_mailinglist_load_group($node);
  

  
  // If not a group post, return. We only send out emails for group posts.
  if (empty($group_node)) {
    return;
  }
  
  $mailer = og_mailinglist_create_mailer();
  
  // Add custom headers.
  $mailer = og_mailinglist_add_web_headers($mailer, $node);

  // Decorate subject.
  $subject = "[" . $group_node->ogm_email . "] " . $node->title;
  
  // Prepare node body for viewing.
  $node = og_mailinglist_render_node_body($node);
  
  // Decorate body.
  $body = og_mailinglist_prepare_web_content($node->body);
  $body = og_mailinglist_build_web_footer($node, $body, 0, TRUE);

  // Let other modules alter outgoing node emails.
  $data = array(
    'subject' => $subject,
    'body' => $body,
    'ogm_email' => $group_node->ogm_email,
    'title' => $node->title,
    'nid' => $node->nid,
    'node' => $node,
  );

  drupal_alter('og_mailinglist_new_node', $data);
  $subject = $data['subject'];
  $body = $data['body'];

  $mailer->Subject = $subject;
  $mailer->Body = $body;
  $mailer->isHTML = TRUE;
  $mailer->AltBody = drupal_html_to_text($body); 
  
  // Add attachments if any.
  if (isset($node->files)) {
    foreach ($node->files as $file) {
      $file = (object) $file;
      $mailer->AddAttachment($_SERVER['DOCUMENT_ROOT'] . base_path() .
                             $file->filepath, $file->filename);
    }
  }


  $mailer = _og_mailinglist_add_addresses($mailer, $node, TRUE);
  
  $success = $mailer->Send();
  
  if ($success) {
    og_mailinglist_log_email_sent('web', $node->nid, 0, $mailer->MessageID, 0, 0, $mailer->MessageID);
  }
  else {
    watchdog('og_mailinglist', "OG_Mailinglist couldn't send a new node email.", NULL,
             WATCHDOG_ERROR);
  }

  return;
}

/*
 * Render nodes according to the default input filter.
 *
 * Code taken and modified from the core node_view function.
 *
 * @param $nid
 *  Node ID
 *
 * @return
 *  Node with rendered body.
 */
function og_mailinglist_render_node_body($node) {
  $node = (object)$node;
  $node->build_mode = OG_MAILINGLIST_BUILD_FULL;
  $node = node_build_content($node, FALSE, TRUE);

  // Set the proper node part, then unset unused $node part so that a bad
  // theme can not open a security hole.
  $content = drupal_render($node->content);
  $node->body = $content; // TODO change this to only add teaser. Add a read more link to actual node / comment
  unset($node->teaser);

  // Allow modules to modify the fully-built node.
  node_invoke_nodeapi($node, 'alter');
  
  return $node;
}

function og_mailinglist_add_web_headers($mailer, $node, $comment = 0, $in_reply_to = 0, $references = 0) {
  $group_node = _og_mailinglist_load_group($node);
  global $base_url;

  $mailer->AddCustomHeader($mailer->HeaderLine("List-Id", "<" . $group_node->ogm_email .
            "@" . variable_get('og_mailinglist_server_string', $_SERVER['SERVER_NAME']) . ">"));
  $mailer->AddCustomHeader($mailer->HeaderLine("List-Post", "<mailto:" . $group_node->ogm_email .
            "@" . variable_get('og_mailinglist_server_string', $_SERVER['SERVER_NAME']) . ">"));
  $mailer->AddCustomHeader($mailer->HeaderLine("List-Archive", url("node/" .
            $group_node->nid, array('absolute' => TRUE))));
  $mailer->AddCustomHeader($mailer->HeaderLine("X-Thread-Url", $base_url .
            "/node/" . $node->nid));
  $mailer->AddCustomHeader($mailer->HeaderLine("X-BeenThere", $group_node->ogm_email .
            "@" . variable_get('og_mailinglist_server_string', $_SERVER['SERVER_NAME'])));

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
            "@" . variable_get('og_mailinglist_server_string', $_SERVER['SERVER_NAME']) . ">"));
  }

  // Set Message-ID.
  $comment_id = (!empty($comment)) ? $comment->cid : 0;
  $mailer->MessageID = "<node-" . $node->nid . "-comment-" . $comment_id . "@" . variable_get('og_mailinglist_server_string', $_SERVER['SERVER_NAME']) . ">";

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
    $author->mail = "anonymous@" . variable_get('og_mailinglist_server_string', $_SERVER['SERVER_NAME']);
    $author_name = "anonymous";
  }
  try {
    $mailer->From = $author->mail;
    $mailer->FromName = $author_name;
    $mailer->AddAddress($group_node->ogm_email . "@" .
            variable_get('og_mailinglist_server_string', $_SERVER['SERVER_NAME']), $group_node->title);

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
