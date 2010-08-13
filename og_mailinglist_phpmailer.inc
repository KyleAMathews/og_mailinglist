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
                                        // TODO this doesn't actually work, need to change references column name apperently as that's a reserved word in mysql? Anyways, errors out right now.
                                        // For now, we'll just use the parent_message_id.
    $references = db_result(db_query("SELECT parent_message_id
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
  $body = og_mailinglist_prepare_web_content($comment->comment); // TODO figure out what the render function for nodes/comments is and use that instead. Current setup inconsistent plus won't shift to other's setups
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
  $mailer->Subject = "[" . $group_node->ogm_email . "] " . $node->title;
  
  // Prepare node body for viewing.
  $node = og_mailinglist_render_node_body($node);
  
  // Decorate body.
  $body = og_mailinglist_prepare_web_content($node->body);
  $body = og_mailinglist_build_web_footer($node, $body, 0, TRUE);

  
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
 *  Node with rendered teaser.
 */
function og_mailinglist_render_node_body($node) {
  $node = (object)$node;
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
