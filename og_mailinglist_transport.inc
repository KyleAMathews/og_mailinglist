<?php

// TODO rewrite header info here.
require_once('og_mailinglist_utilities.inc');
require_once('og_mailinglist_api.inc');
og_mailinglist_mimeDecode_load_library();
og_mailinglist_phpmailer_load_library();

###############################################################################
###   Original version written by Conan Albrecht   March 2009
/**
 * Receive messages POSTed from an external source.
 *
 * This function enables messages to be sent to a Drupal site via POST or some other RFC822
 * source input (e.g. directly from a mail server).
 *
 */
function og_mailinglist_post() {
  $message = $_POST['message'];
  $token = $_POST['token'];
  $group_name = $_POST['group_name'];

  $hash = md5(variable_get('og_mailinglist_incoming_key', '**') . $message);
  if ($hash != $token) {
    watchdog('access denied', 'Authentication error for POST e-mail. Check that you\'ve set the validation string in /admin/settings/og_mailinglist and in site_info.php', NULL, WATCHDOG_WARNING);
    return drupal_access_denied();
  }
  _og_mailinglist_process_email($message, $group_name);
}

function _og_mailinglist_process_email($raw_email, $group_name) {
  try {
    $email = array();

    $email['original_email_text'] = $raw_email;
    
    // Extract all the needed info from the email into a simple array.
    $email = _og_mailinglist_parse_email($email);

    // If the x-BeenThere header is set, this is an email that was sent out and
    // looped back to the server. Reject it.
    if (!empty($email['headers']['x-beenthere'])) {
      exit();
    }

    if (empty($email['headers']['subject'])) {
      $email['headers']['subject'] = t('(no subject)');
    }

    // Parse the group name from the email. Regex borrowed from valid_email_address().
    $server_string = variable_get("og_mailinglist_server_string", "");
    if (empty($group_name)) {
      if (preg_match("/([a-zA-Z0-9_\-\.\+\^!#\$%&*+\/\=\?\`\|\{\}~\']+)@" . $server_string . "/", $email['headers']['to'], $matches)) {
        $group_name = $matches[1];
      }
      if (preg_match("/([a-zA-Z0-9_\-\.\+\^!#\$%&*+\/\=\?\`\|\{\}~\']+)@" . $server_string . "/", $email['headers']['cc'], $matches)) {
        $group_name = $matches[1];
      }
    }
    
    // Did we actually get email text back? If not, throw an exception.
    if ($email['mailbody'] == "") {
      throw new Exception(t("Could not parse message body from the text/plain portion of the email."));
    }
    
    // Check the size of the body and kick out if too large (for security).
    $max_size = variable_get('og_mailinglist_max_message_size', 200);  // 200kb
    if (strlen($email['mailbody']) > $max_size * 1024 && $max_size != 0) {
      throw new Exception(t("Discussion items sent via email must be less than !max_size Kb. For security reasons, please post larger messages through the web interface.", array("!max_size" => $max_size)));
    }
  
    // This regex worries me... do *all* email clients place email addresses between <>?
    // Get the user id.
    $mailfrom = $email['headers']['from'];
    if (preg_match("/<(.*?)>/", $email['headers']['from'], $matches)) { 
      $mailfrom = $matches[1];
    }
    if (!$email['userid'] = db_result(db_query("SELECT uid
                                      FROM {users}
                                      WHERE mail='%s'", $mailfrom))) {
      // If not posting from their normal email account, let's try their mail alias.
      $email['userid'] = db_result(db_query("SELECT uid
                                            FROM {users}
                                            WHERE data LIKE '%%%s%%'", $mailfrom));
    }
    if (!$email['userid']) {
      $exception = t("Could not locate the user account for !mailfrom. For security reasons, please post from the email account you are registered with.", array('!mailfrom' => $mailfrom));
      if (module_exists('mailalias')) {
        $exception .= t("If you'd like to post from an email account different than the one you registered with, edit your account page to add that email address as an email alias.");

      }
      throw new Exception($exception);
    }
    // Check how many posts have been made by this user (for security).
    if (variable_get('og_mailinglist_max_posts_per_hour', 20) > 0) {
      $one_hour_ago = time() - (60 * 60);
      $num_recent_posts = db_result(db_query("SELECT count(*)
                                             FROM {node}
                                             WHERE uid=%d AND
                                             created > %d",
                                             $email['userid'], $one_hour_ago));
      if ($num_recent_posts > variable_get('og_mailinglist_max_posts_per_hour', 20)) {
       throw new Exception(t("You have posted via email too many times in the last hour.  For security reasons, please wait a while or post through the regular web interface."));
      }
    }
    
    // Get the group id.
    $email['groupid'] = db_result(db_query("SELECT nid
                                           FROM {og_mailinglist}
                                           WHERE LOWER(group_email)='%s'", $group_name));
    if (!$email['groupid']) { 
      throw new Exception(t("Could not locate group named !group_name", array('!group_name' => $group_name)));
    }
    
    // Check the this user is a member of this group (for security).
    $results = db_query("SELECT og.nid, n.title
                        FROM {og_uid} og JOIN {node} n
                        JOIN {og} o
                        WHERE og.uid=%d AND
                        og.nid=%d AND
                        og.nid=n.nid AND
                        o.nid=og.nid", $email['userid'], $email['groupid']);   
    if (!db_result($results)) { // TODO also check if person is subscribed to this thread -- if they are, let them comment by email.
      throw new Exception(t("You (email: !mailfrom, account page: !account_page) are not a member of this group.  Please join the group via the web site before posting.",
                               array('!mailfrom' => $mailfrom, '!account_page' => url("user/" . $email['userid'], array('absolute' => TRUE)))));
    }
    
    // Try to get the Node ID. If the email is a comment to a node, we should be
    // able to get the Node ID of the original node.
    $email['nid'] = og_mailinglist_parse_nid($email['text_plain'], $email['structure']->headers);
  
    // create the new content in Drupal.
    if (!empty($email['nid'])) { // a new comment
      
      // Two checks first before creating the comment.
      // There are at least two reasons why an email could have a nid but not be
      // intended as a new comment.
      // First, someone could be forwarding an email to a different group.
      // Second, it's common on mailinglists to fork threads by changing
      // the subject line. We need to check for both.
      
      // Does the detected nid belong to the same group as the email was forwarded to?
      $nid_groupid = db_result(db_query("SELECT group_nid
                                        FROM {og_ancestry}
                                        WHERE nid = %d", $email['nid']));
      
      if ($nid_groupid != $email['groupid']) {
        og_mailinglist_save_node($email);
        exit(0); // So we don't save a comment as well
      }

      // Check if the thread was forked by comparing the email subject to the node's title.
      // The levenshtein distance between a reply and node title should be 4 (e.g. "a subject" and "Re: a subject").
      // I added a few more to the distance to account for oddness in foreign languages but
      // is still small enough to catch any substantive subject change.
      $node = node_load($email['nid']);

      // Remove the group identifier (e.g. [a-group]) from the subject as it's not in the node title.
      $treated_subject =  preg_replace("/^[RrEe:]{3}\s\[[\w-]*\]/", "", $email['headers']['subject']);

      if (levenshtein($node->title, $treated_subject) > 7) {
        // The subject was changed so we'll create a new node now intead of a comment.
        // Unset the in-reply-to and references headers so this new thread doesn't get merged with the old thread.
        unset($email['headers']['in-reply-to']);
        unset($email['structure']->headers['in-reply-to']);
        unset($email['headers']['references']);
        unset($email['structure']->headers['references']);
        og_mailinglist_save_node($email);
        exit(0); // So we don't save a comment as well.
      }
      
      // If we got this far, the email is definitely intended as a new comment.
      og_mailinglist_save_comment($email);
      
    }
    else {  // A new node.
      og_mailinglist_save_node($email);
    }
    
    // Tell Exim4 we had success!
    exit(0);  
  
  }catch (Exception $e) {
    try {
      // Compose an email back to the sender informing them of the problem.
      $head = Array();
      $head[] = 'From: ' . variable_get("og_mailinglist_noreply_email", t("no-reply@" . variable_get("og_mailinglist_server_string", "example.com")));
      $head[] = 'References: ' . $email['headers']['message-id'];
      $head[] = 'In-Reply-To: ' . $email['headers']['message-id'];
      $errormsg = $e->getMessage();
      $msgdate = $email['headers']['date'];
      $msgfrom = $email['headers']['from'];
      $commentedbody = str_replace("\n", "\n> ", $mailbody);
      $body = "An error occurred while processing your submission:
      
       $errormsg
  
  Please correct this error and try again, or contact the system administrator.  Thank you.
  
  On $msgdate, $msgfrom wrote:
  > $commentedbody";
      
      // send it off
      if (!mail($email['headers']['from'], "Error processing message", $body, implode("\n", $head))) {
        throw new Exception("Mail error");
      }
    
      // print error message to log, then quit
      echo t("Error: !message\n", array('!message' => $e->getMessage()));
      exit(0);
      
    }catch (Exception $e2) {
      // if we get here, we couldn't even send an email back to sender, so just have Exim compose an error message and send back
      echo t("Error: !e2 ::: Embedded Error: !e\n", array('!e2' => $e2->getMessage(), '!e' => $e->getMessage()));
      exit(1);
    }
  }
}

function og_mailinglist_save_comment($email) {
  $nid = $email['nid'];
  
  // set the user account to this poster (comment_save checks the global user rights)
  global $user;
  $user = user_load($email['userid']);

  // check that this user has rights to post comments
  if (!user_access('post comments')) {
    throw new Exception(t("You do not have rights to post comments in the system."));
  }
  
  // check that this post has comments enabled
  if (node_comment_mode($nid) != COMMENT_NODE_READ_WRITE) {
    throw new Exception(t("Comments have been disabled for this post."));
  }

  $mailbody = $email['mailbody'];

  // create an array representing the comment
  $comment = array();
  $comment['uid'] = $email['userid'];
  $comment['nid'] = $nid;
  
  ////// DISABLED this so we don't have threaded messages
  //if (FALSE && $messageid['cid']) {
  //  $comment['pid'] = $messageid['cid'];
  //}else{
  //  $comment['pid'] = 0;
  //}
  
  if (preg_match("/re:\s*\[.*?\]\s*(.*)/i", $email['headers']['subject'], $matches)) {
    $comment['subject'] = $matches[1];
  }elseif (preg_match("/re: +(.*)/i", $email['headers']['subject'], $matches)) {
    $comment['subject'] = $matches[1];
  }else{
    $comment['subject'] = $email['headers']['subject'];
  }
  $comment['comment'] = $mailbody;
  
  // Get the cid that'll be used. Yes, this isn't a very pretty way to do this.
  // If someone else creates a comment between now and when the comment is
  // actually created, two emails will be sent out for this comment.
  $cid = 1 + db_result(db_query("SELECT cid FROM {comments} ORDER BY cid DESC LIMIT 1"));
  
  // Log that comment came from email so og_mailinglist_phpmailer doesn't send an email as well.
  $parent_message_id = _og_mailinglist_get_thread_parent_messageid($email['headers']['in-reply-to'], $email['headers']['references']);
  og_mailinglist_log_email_sent('email', $nid, $cid, $email['headers']['message-id'], $email['headers']['in-reply-to'], $email['headers']['references'], $parent_message_id);
  
  // Save the new comment.
  $cid = comment_save($comment);  
  
  if (!$cid) {
    throw new Exception(t("An unknown error occurred while saving your comment."));
  }  
  
  
  $node = node_load(array('nid' => $nid));
  $comment['cid'] = $cid; // Not sure why this isn't added automatically.
  _og_mailinglist_email_comment_email($email, $node, $comment);

  $sender = user_load($email['userid']);
  $group_node = node_load($email['groupid']);
  // Report activity to watchdog.
  watchdog('og_mailinglist', t("Posted comment for @mailfrom to group @group_name for nid=@nid with cid=@cid",
      array('@mailfrom' => $sender->name, '@group_name' => $group_node->title,
        '@nid' => $node->nid, '@cid' => $cid)));
} 
 
function og_mailinglist_save_node($email) {
  $mailbody = $email['mailbody'];

  // Get the nid that'll be used. Yes, this isn't a very pretty way to do this.
  // If someone else creates a node between now and when the node is
  // actually created, two emails will be sent out for this node.
  $nid = 1 + db_result(db_query("SELECT nid FROM {node} ORDER BY nid DESC LIMIT 1"));
  
  // Log that comment came from email so og_mailinglist_phpmailer doesn't send an email as well.
  og_mailinglist_log_email_sent('email', $nid, $cid, $email['headers']['message-id'], 0, 0, $email['headers']['message-id']);
  
  // Is the group public? If not, we need to set the node as private.
  $is_private = db_result(db_query("SELECT og_private
                                   FROM {og}
                                   WHERE nid = %d", $email['groupid']));
  
  // The og_private table stores a 1 if the group is private and 0 if it's public.
  // node_save() expects the opposite so we'll invert the value of $is_private.
  $is_public = TRUE;
  if ($is_private) {
    $is_public = FALSE;
  }
  
  // Create the new node
  $node->title = $email['headers']['subject'];
  $node->uid = $email['userid'];
  $node->created = time();
  $node->status = 1; // published
  $node->promote = 0;
  $node->sticky = 0;
  $node->body = $mailbody;
  $node->teaser = node_teaser($mailbody);
  $node->type = variable_get('og_mailinglist_default_content_type', 'story');
  $node->og_public = $is_public;
  $node->og_groups[$email['groupid']] = $email['groupid'];
  $node->comment = variable_get("comment_$node_type", COMMENT_NODE_READ_WRITE);
  
  //// Add attachments if any.TODO fix this someday. Best idea -- save mail objects w/ attachments. On cron scoop them up and add them to nodes/comments
  //if (isset($email['attachments'])) {
  //  $nodeattachments = _og_mailinglist_save_attachments_temp_dir($email['attachments']);
  //  $node->og_mailinglist_attachments = $nodeattachments;
  //  _og_mailinglist_save_files($node);
  //}

  // Let other modules alter the node object before saving.
  drupal_alter('og_mailinglist_save_node', $node);

  node_save($node);
  
  // Send off email.
  _og_mailinglist_email_node_email($email, $node);

  $sender = user_load($email['userid']);
  $group_node = node_load($email['groupid']);
  // Report activity to watchdog.
  watchdog('og_mailinglist', t("Posted node for @mailfrom to group @group_name with nid=@nid.",
      array('@mailfrom' => $sender->name, '@group_name' => $group_node->title,
        '@nid' => $node->nid)));
}

function _og_mailinglist_email_node_email($email, $node) {
  // Build new email.
  $email = _og_mailinglist_rewrite_headers($email, $node, TRUE);
  $footer = _og_mailinglist_build_footer($node);
  $email = _og_mailinglist_add_footer($email, $footer);
  $email['new_email_text'] = _og_mailinglist_encode_email(array($email['structure']));
  
  // Send it off.
  _og_mailinglist_send_raw_email($email['new_email_text']);

  // If the sender's subscription type isn't email, give him a thread subscription.
  if (og_mailinglist_get_group_subscription_type($node->og_groups[0], $node->uid) != "email") {
    og_mailinglist_save_thread_subscriptions($node->nid, array($node->uid));
  }
}

function _og_mailinglist_email_comment_email($email, $node, $comment) {
  // Build new email.
  $email = _og_mailinglist_rewrite_headers($email, $node, FALSE, $comment);
  $footer = _og_mailinglist_build_footer($node);
  $email = _og_mailinglist_add_footer($email, $footer);
  $email['new_email_text'] = _og_mailinglist_encode_email(array($email['structure']));
  
  // Send it off.
  _og_mailinglist_send_raw_email($email['new_email_text']);
}

function _og_mailinglist_parse_email($email) {
  $params['include_bodies'] = true; 
  $params['decode_bodies'] = true; 
  $params['decode_headers'] = true; 
  $params['input'] = $email['original_email_text'];
  
  // do the decode
  $email['structure'] = clone $structure = Mail_mimeDecode::decode($params);

  // Copy headers to $email array.
  $email['headers'] = array_copy($structure->headers);

  if (!empty($structure->parts)) {
    foreach ($structure->parts as $i => $part) {
      if (!empty($part->parts)) {
        $subPart = $part->parts;

        foreach ($subPart as $j => $subPartPart) {
          if (isset($subPartPart->disposition) and ($subPartPart->disposition==='attachment')) {
            $email['attachments'][] = _og_mailinglist_parse_attachment($subPartPart);
          }
          elseif ($subPartPart->ctype_secondary == 'plain') {
            $email['text'] = _og_mailinglist_parse_content($subPartPart);
          }
          elseif ($subPartPart->ctype_secondary == 'html') {
            $email['html'] = _og_mailinglist_parse_content($subPartPart);
          }
        }
      }

      if (isset($part->disposition) and ($part->disposition==='attachment')) {
        $email['attachments'][] = _og_mailinglist_parse_attachment($part);
      }
      elseif (($part->ctype_secondary == 'plain') && empty($email['text'])) {
        $email['text'] = _og_mailinglist_parse_content($part);
      }
      elseif (($part->ctype_secondary == 'html') && empty($email['html'])) {
        $email['html'] = _og_mailinglist_parse_content($part);
      }
    }
  }
  // Make sure our text and html parts are accounted for
  if (isset($email['html']) && !isset($email['text'])) {
    $email['text'] = preg_replace('|<style.*</style>|mis', '', $email['html']);
    $email['text'] = drupal_html_to_text($email['text']);
  }
  elseif (isset($email['text']) && !isset($email['html'])) {
    $mail['html'] = check_markup($email['text']);
  }

  // Last ditch attempt - use the body as-is.
  if (!isset($email['text'])) {
    // Try to convert character set to UTF-8.
    if (!empty($structure->ctype_parameters['charset'])) {
      $email['text'] = drupal_convert_to_utf8($structure->body, $structure->ctype_parameters['charset']);
    } else {
      $email['text'] = $structure->body;
    }
    $email['html'] = check_markup($email['text']);
  }

  // Convert the subject to UTF-8.
  if (!empty($structure->ctype_parameters['charset'])) {
    $email['headers']['subject'] = drupal_convert_to_utf8($email['headers']['subject'], $structure->ctype_parameters['charset']);
  }

  // TODO figure out how to pull out sane HTML w/o funky css or other oddities.
  $email['mailbody'] = $email['text'];
  $email['isHTML'] = false;
 
  // Save copy of the original mailbody
  $email['orig_mailbody'] = $email['mailbody']; 
 
  return $email;  
}
// TODO fix attachments for filefield

function _og_mailinglist_parse_attachment($part) {
  $info['data'] = $part->body;
  $info['filemime'] = $part->ctype_primary . "/" . $part->ctype_secondary;
  $info['filename'] = $part->ctype_parameters['name'];

  return $info;
}

function _og_mailinglist_parse_content($part) {
  $content = $part->body;

  // Try to convert character set to UTF-8.
  if (!empty($part->ctype_parameters['charset'])) {
    $content = drupal_convert_to_utf8($content, $part->ctype_parameters['charset']);
  }

  return $content;
}

function _og_mailinglist_save_files(&$node) {
  global $user;
  $user = user_load(array('uid' => $node->uid));
  
  // If $node->og_mailinglist_attachments is empty or upload not installed just return
  if (!$node->og_mailinglist_attachments || !module_exists('upload')) {
    return;
  }

  // If user doesn't have upload permission then don't bother processing
  // TODO check comment upload permissions.
  if (!(user_access('upload files'))) {
    //echo "didn't have permissions?\n\n";
    return;
  }
  
  // Convert $node->og_mailinglist_attachments to $node->files ready for upload to use
  foreach ($node->og_mailinglist_attachments as $filekey => $attachment) {
  
    $limits = _upload_file_limits($user);
    $validators = array(
      'file_validate_extensions' => array($limits['extensions']),
      'file_validate_image_resolution' => array($limits['resolution']),
      'file_validate_size' => array($limits['file_size'], $limits['user_size']),
    );
    
    if ($file = _og_mailinglist_save_file($attachment, $validators)) {
      // Create the $node->files elements
      $file->list = variable_get('upload_list_default', 1);
      $file->description = $file->filename;
      $node->files[$file->fid] = $file;

      // This is a temporary line to get upload_save to work (see upload.module line 413)
      // upload_save checks for either the presence of an old_vid, or the session variable, to determine
      // if a new upload has been supplied and create a new entry in the database
      $node->old_vid = 1;
    }

  }

  // Destroy $node->og_mailinglist_attachments now we have created $node->files
  unset($node->og_mailinglist_attachments);

}


// This started as a copy of file_save_upload
//function _og_mailinglist_node_file($attachment, $source, $validators = array(), $dest = FALSE, $replace = FILE_EXISTS_RENAME) {
function _og_mailinglist_save_file($attachment, $validators = array()) {
  global $user;

  // Add in our check of the the file name length.
  $validators['file_validate_name_length'] = array();

  // Build the list of non-munged extensions.
  // @todo: this should not be here. we need to figure out the right place.
  $extensions = '';
  foreach ($user->roles as $rid => $name) {
    $extensions .= ' '. variable_get("upload_extensions_$rid",
    variable_get('upload_extensions_default', 'jpg jpeg gif png txt html doc xls pdf ppt pps odt ods odp'));
  }
  
  // Begin building file object.
  $file = new stdClass();
  $file->filename = file_munge_filename(trim(basename($attachment['filename']), '.'), $extensions);
  $file->filepath = $attachment['filepath'];
  $file->filemime = file_get_mimetype($file->filename);;

  // Rename potentially executable files, to help prevent exploits.
  if (preg_match('/\.(php|pl|py|cgi|asp|js)$/i', $file->filename) && (substr($file->filename, -4) != '.txt')) {
    $file->filemime = 'text/plain';
    $file->filepath .= '.txt';
    $file->filename .= '.txt';
  }

  // Create temporary name/path for newly uploaded files.
  //if (!$dest) {
    $dest = file_destination(file_create_path($file->filename), FILE_EXISTS_RENAME);
  //}
  //$file->source = $source;
  $file->destination = $dest;
  $file->filesize = $attachment['filesize'];
  
  // Call the validation functions.
  $errors = array();
  foreach ($validators as $function => $args) {
    array_unshift($args, $file);
    $errors = array_merge($errors, call_user_func_array($function, $args));
  }

  // Check for validation errors.
  if (!empty($errors)) {
    watchdog('mailhandler', 'The selected file %name could not be uploaded.', array('%name' => $file->filename), WATCHDOG_WARNING);
    while ($errors) {
      watchdog('mailhandler', array_shift($errors));
    }
    return 0;
  }

  // Move uploaded files from PHP's tmp_dir to Drupal's temporary directory.
  // This overcomes open_basedir restrictions for future file operations.
  $file->filepath = $file->destination;
  if (!file_move($attachment['filepath'], $file->filepath)) {
    watchdog('mailhandler', 'Upload error. Could not move file %file to destination %destination.', array('%file' => $file->filename, '%destination' => $file->filepath), WATCHDOG_ERROR);
    return 0;
  }

  // If we made it this far it's safe to record this file in the database.
  $file->uid = $user->uid;
  $file->status = FILE_STATUS_TEMPORARY;
  $file->timestamp = time();
  drupal_write_record('files', $file);
  
  // Return the results of the save operation
  return $file;

}

function _og_mailinglist_save_attachments_temp_dir($attachments) {
  // Parse each mime part in turn
  foreach ($attachments as $info) {
    // Save the data to temporary file
    $temp_file = tempnam(file_directory_temp(), 'mail');
    $filepath = file_save_data($info['data'], $temp_file);
  
    // Add the item to the attachments array, and sanitise filename
    $node_attachments[] = array(
      'filename' => _og_mailinglist_sanitise_filename($info['filename']),
      'filepath' => $filepath,
      'filemime' => strtolower($info['filemime']),
      'filesize' => strlen($info['data']),
    );
  }
  file_save_data("hello world", file_directory_path() . "/temp");
  
  // Return the attachments
  return $node_attachments;

}

/**
 * Take a raw attachment filename, decode it if necessary, and strip out invalid characters
 * Return a sanitised filename that should be ok for use by modules that want to save the file
 */
function _og_mailinglist_sanitise_filename($filename) {
  // Decode multibyte encoded filename
  $filename = mb_decode_mimeheader($filename);

  // Replaces all characters up through space and all past ~ along with the above reserved characters to sanitise filename
  // from php.net/manual/en/function.preg-replace.php#80431

  // Define characters that are  illegal on any of the 3 major OS's
  $reserved = preg_quote('\/:*?"<>|', '/');

  // Perform cleanup
  $filename = preg_replace("/([\\x00-\\x20\\x7f-\\xff{$reserved}])/e", "_", $filename);

  // Return the cleaned up filename
  return $filename;
}

function _og_mailinglist_create_new_email($email) {
  $structure = clone $email['structure'];
  $structure = _og_mailinglist_rewrite_headers($structure, $email);
  $structure = _og_mailinglist_add_footer($structure, $email);
  $email['new_email_text'] = _og_mailinglist_encode_email(array($structure));
  
  return $email;
}

// Turn structure back into a plain text email using recursion.
function _og_mailinglist_encode_email($structure, $boundary = "", $email = "") {
  foreach($structure as $part) {   
    if (empty($boundary)) {
      $boundary = $part->ctype_parameters['boundary'];
    }
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
      // Encode the body as base64 if necessary
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
    // because mime_decode decodes that and PHP doesn't have a function
    // AFAIK to reencode the text as quoted-printable.
    if ($value && $value !== "quoted-printable") { 
      $header .= capitalize_headers($key, " -") . ": " . $value . "\n";  
    }
  }
  
  return $header;
}

// Keep mime-version, date, subject, from, to, and content-type
function _og_mailinglist_rewrite_headers($email, $node, $new_node = FALSE, $comment = 0) {
  $group_node = _og_mailinglist_load_group($node);
  $headers = $email['structure']->headers;
  $new_headers = array();
  $new_headers['mime-version'] = $headers['mime-version'];
  $new_headers['date'] = $headers['date'];
  if ($new_node) {
    $new_headers['subject'] = "[" . $group_node->ogm_email . "] " . $node->title;  
  }
  else {
    $new_headers['subject'] = $headers['subject'];
  }
  
  $new_headers['from'] = $headers['from'];
  $new_headers['to'] = $group_node->ogm_email . "@" . variable_get('og_mailinglist_server_string', 'example.com');
  $new_headers['bcc'] =
    array_to_comma_delimited_string(
      _og_mailinglist_get_subscribers($node, $new_node));
  $new_headers['content-type'] = $headers['content-type'];
  $new_headers['content-transfer-encoding'] =  $headers['content-transfer-encoding'];
  
  // Add a reply-to header directed to the group if enabled.
  if (variable_get('og_mailinglist_reply_to_group', FALSE)) {
    $new_headers['reply-to'] = $group_node->ogm_email . "@" . variable_get('og_mailinglist_server_string', 'example.com');
  }

  // Add list headers.
  $new_headers['List-Id'] = "<" . $group_node->ogm_email . "@" .
                variable_get('og_mailinglist_server_string', 'example.com') . ">";
  $new_headers['List-Post'] = "<mailto:" . $group_node->ogm_email . "@" .
                variable_get('og_mailinglist_server_string', 'example.com') . ">";
  $new_headers['List-Archive'] = url("node/" . $group_node->nid, array('absolute' => TRUE));
  
  // Thread-URL header.
  global $base_url;
  $new_headers['X-Thread-Url'] = $base_url . "/node/" . $node->nid;
  
  // Set x-BeenThere header
  $new_headers['X-BeenThere'] = $group_node->ogm_email . "@" .
                variable_get('og_mailinglist_server_string', 'example.com');

  // Set Message-Id
  $new_headers['Message-ID'] = $email['headers']['message-id'];
  
  // Set References
  $new_headers['References'] = $email['headers']['references'];

  // Set In-Reply-To.
  $new_headers['in-reply-to'] = $email['headers']['in-reply-to'];
  
  $email['structure']->headers = $new_headers;
  
  return $email;
}

function _og_mailinglist_add_footer($email, $footer) {
  $headers = $email['structure']->headers;
  $structure = $email['structure'];
  
  // If message is 7/8bit text/plain and uses us-ascii charecter set, just 
  // append the footer.
  if (preg_match('/^text\/plain/i', $headers['content-type']) &&
      isset($structure->body)) {
     $structure->body .= "\n" . $footer;
  }
  // If message is already multipart, just append new part with footer to end
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
    $boundary = "Drupal-OG-Mailing-List--" . rand(100000000, 9999999999999);
    
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
  
  $email['structure'] = $structure;
  
  return $email;
}

function _og_mailinglist_send_raw_email($raw_email) {
  $rand_filename = rand(1000, 10000000000);
  file_put_contents("/tmp/" . $rand_filename, $raw_email . "\n");
  system("/usr/sbin/sendmail -t < /tmp/" . $rand_filename);
}

/*
 * Try to parse nid from the email.
 */
function og_mailinglist_parse_nid($email, $headers) {
  $nid = "";
  // Check the in-reply-to header.
  $parent_message_id = _og_mailinglist_get_thread_parent_messageid($headers['in-reply-to'], $headers['references']);
  $nid = _og_mailinglist_get_nid_from_parent_messageid($parent_message_id);

  if (!empty($nid)) {
    return $nid;
  }
  else if (preg_match("/og_mailinglist\/unsubscribe\/([0-9]+)/s", $email, $matches)) {
    $nid = $matches[1];
  }
  // Else, as last resort, pick the last node that matches the title in the subject line.
  else {
    $nid = _og_mailinglist_nid_of_subject($headers['subject']);
  }
  return $nid;
}

function _og_mailinglist_nid_of_subject($subject) {
  $nid = "";
  preg_match("/[Rr][Ee]:\s\[.*?\]\s(.+)/s", $subject, $matches);
  $title = $matches[1];
  if ($title) {
      $nid = db_result(db_query("SELECT nid
                               FROM {node}
                               WHERE title like '%%s%'
                               ORDER BY created desc
                               LIMIT 1", $title));
  }
  return $nid;
}
