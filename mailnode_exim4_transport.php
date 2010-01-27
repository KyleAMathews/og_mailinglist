#!/usr/bin/php
<?php

require_once("/usr/share/php/Mail/mimeDecode.php");
require_once("phpmailer/class.phpmailer.php");
// Require the QueryPath core. 
require_once('QueryPath/QueryPath.php');
require_once('mailnode_utilities.inc');
require_once('mailnode_api.inc');

###############################################################################
###   This script is called from Exim4.  It is a "pipe" type of 
###   transport that takes an email and processes it.  It also requires
###   a router file: mailnode_exim4_router.php
###
###   Written by Conan Albrecht   March 2009
###
###   Here's the code that needs to go into Exim4's configuration:
###   (note you need to customize the path in the command line)
###
###   drupal_mailnode:
###     driver = pipe
###     path = "/bin:/usr/bin:/usr/local/bin"
###     command = /var/mailnode/mailnode_exim4_transport.php $local_part
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
###       ./mailnode_exim4_transport.php groupname < email.txt
###
###       where email.txt is an email saved to a file.
###

try {
  # boostrap drupal
  require_once('mailnode_exim4_boostrap_command_line.php');

  $email = array();
  
  # set command line arguments (sent by the exim4 transport) to variables we can read
  $mail_username = $argv[1];

  # grab the email message from stdin, then parse the parts
  # we only use the text/plain part right now
  $fd = fopen("php://stdin", "r");
  while (!feof($fd)) {
    $email['original_email_text'] .= fread($fd, 1024);
  }
  
  // Detect email character set.
  $email['char_set'] = _mailnode_detect_email_char_set($email['original_email_text']);
  
  dd_log("this this is on");
  dd_log($email);
  // Extract all the needed info from the email into a simple array.
  $email = _mailnode_parse_email($email);
  
  if ($email['mailbody'] == "") {
    throw new Exception(t("Could not parse message body from the text/plain portion of the email."));
  }
  
  # check the size of the body and kick out if too large (for security)
  if (strlen($email['mailbody']) >
      variable_get('mailnode_max_message_size', 200) * 1024) {  # 200 Kb
    throw new Exception(t("Discussion items sent via email must be less than 200 Kb Mb. For security reasons, please post larger messages through the web interface."));
  }

  // This regex worries me -- do email clients *always* place addresses between <>?
  # get the user id
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
  if (!$email['userid']) { // TODO -- add suggestion to add an alias to their account if they post from another email account at times.
    throw new Exception(t("Could not locate the user account for $mailfrom.  For security reasons, please post from the email account you are registered with."));
  }
  # check how many posts have been made by this user (for security)
  if (variable_get('mailnode_max_posts_per_hour', 20) > 0) {
    $one_hour_ago = time() - (60 * 60);
    $num_recent_posts = db_result(db_query("SELECT count(*)
                                           FROM {node}
                                           WHERE uid=%d AND
                                           created > %d",
                                           $email['userid'], $one_hour_ago));
    if ($num_recent_posts > variable_get('mailnode_max_posts_per_hour', 20)) {
     throw new Exception(t("You have posted via email too many times in the last hour.  For security reasons, please wait a while or post through the regular web interface."));
    }
  }
  
  # get the group id
  $email['groupid'] = db_result(db_query("SELECT id
                                         FROM {purl}
                                         WHERE provider='spaces_og' AND
                                         LOWER(value)='%s'", $mail_username));
  if (!$email['groupid']) { 
    throw new Exception(t("Could not locate group named $mail_username"));
  }
  
  # check the this user is a member of this group (for security)
  $results = db_query("SELECT og.nid, n.title
                      FROM {og_uid} og JOIN {node} n
                      JOIN {og} o
                      WHERE og.uid=%d AND
                      og.nid=%d AND
                      og.nid=n.nid AND
                      o.nid=og.nid", $email['userid'], $email['groupid']);   
  if (!db_result($results)) {
    throw new Exception(t("You are not a member of this group.  Please join the group via the web site before posting."));
  }
  
  # get the Node ID (if we're replying to a discussion/comment email sent from our module.
  $email['nid'] = mailnode_parse_nid($email['original_email_text'], $email['structure']->headers['subject']);
  dd_log($email);
  // create the new content in Drupal.
  if ($email['nid']) { # a new comment
    // Two checks. There are at least two reasons why an email could have a nid
    // but not be intended as a new comment.
    // First, someone could be forwarding an email to a different group.
    // Second, it's common on mailinglists to fork discussions by changing
    // the subject line. We need to check for both.
    
    // Does the detected nid belong to the same group as the email was forwarded to?
    $nid_groupid = db_result(db_query("SELECT group_nid
                                      FROM {og_ancestry}
                                      WHERE nid = %d", $email['nid']));
    
    if ($nid_groupid != $email['groupid']) {
      mailnode_save_discussion($email);
      exit(0); // So we don't save a comment as well
    }
    
    // Is the subject line different than the expected node title?
    // If the subject_nid is empty, that means the subject is new so email is new discussion.
    // If the subject_nid is different, that also means the email is a new discussion but
    // that it coincidentally matched an earlier discussion.
    $subject_nid = _mailnode_nid_of_subject($email['structure']->headers['subject']);
    if (!empty($subect_nid) || $subject_nid != $email['nid']) {
      mailnode_save_discussion($email);
      exit(0); // So we don't save a comment as well.
    }
    
    // If we got this far, the email is definitely intended as a new comment.
    mailnode_save_comment($email);
    
  }else {  # a new discussion
    mailnode_save_discussion($email);
  }
  
  # tell Exim4 we had success!
  exit(0);  

}catch (Exception $e) {
  try {
    # compose an email back to the sender
    $head = Array();
    $head[] = 'From: ' . variable_get("mailnode_noreply_email", t("no-reply@" . variable_get("mailnode_server_string", "example.com")));
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
    
    # send it off
    if (!mail($email['headers']['from'], "Error processing message", $body, implode("\n", $head))) {
      throw new Exception("Mail error");
    }
  
    # print error message to log, then quit
    echo t("Error: " . $e->getMessage() . "\n");
    exit(0);
    
  }catch (Exception $e2) {
    # if we get here, we couldn't even send an email back to sender, so just have Exim compose an error message and send back
    echo t("Error: ") . $e2->getMessage() . " ::: Embedded Error: " . $e->getMessage() . "\n";
    exit(1);
  }
}

function mailnode_save_comment($email) {
  $nid = $email['nid'];
  
  # set the user account to this poster (comment_save checks the global user rights)
  global $user;
  $user = user_load($email['userid']);

  # check that this user has rights to post comments
  if (!user_access('post comments')) {
    throw new Exception(t("You do not have rights to post comments in the system."));
  }
  
  # check that this discussion has comments enabled
  if (node_comment_mode($nid) != COMMENT_NODE_READ_WRITE) {
    throw new Exception(t("Comments have been disabled for this discussion."));
  }
  
  //$mailbody = preg_replace("/Â/", "", $email['mailbody']); // TODO figure out why seperator scrambled rather than brute forcing fix.
  $mailbody = $email['mailbody'];
  
  $email = tidyEmail($email);
  $mailbody = $email['mailbody'];
  
  # ensure the body is not empty
  if (empty($mailbody)) {
    throw new Exception(t("Could not parse message body from your email.  Did you remove the separator text (e.g. 'Reply above this line')?"));
  }

  # create an array representing the comment
  $comment = array();
  $comment['uid'] = $email['userid'];
  $comment['nid'] = $nid;
  ### DISABLED this so we don't have threaded messages
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
  
  // Get the cid that'll be used. Yes, this isn't a very good way to do this.
  // It's not thread-safe as if someone else saves comment between us logging
  // and the comment actually being created. But in that case, email is just sent
  // twice so not a huge deal.
  $cid = 1 + db_result(db_query("SELECT cid FROM {comments} ORDER BY cid DESC LIMIT 1"));
  
  // Log that comment came from email so mailnode_phpmailer doesn't send an email as well.
  mailnode_log_email_sent('email', $nid, $cid);
  
  // Save the new comment.
  $cid = comment_save($comment);  
  
  if (!$cid) {
    # why does comment_save just give FALSE rather than a nice Exception when it fails?
    throw new Exception(t("An unknown error occurred while saving your comment."));
  }  
  
  # save a message to the mail log
  echo t("Posted comment for $mailfrom to group $mail_username for node=$nid with cid=$cid.");
  
  $node = node_load(array('nid' => $nid));
  $comment['cid'] = $cid; // Not sure why this isn't added automatically.
  _mailnode_email_comment_email($email, $node, $comment);
} 
 
function mailnode_save_discussion($email) {
  
  //# clean up the email
  //if (variable_get('mailnode_cleaner', 0) == 1) {
  //  $email = mailnode_clean_email($email);
  //}
  $email = tidyEmail($email);
  $mailbody = $email['mailbody'];
  
  # ensure the body is not empty
  if (empty($mailbody)) {
    throw new Exception(t("Your message body was empty."));
  }

  // Get the nid that'll be used. Yes, this isn't a very good way to do this.
  // It's not thread-safe as if someone else saves a node between us logging
  // and the node actually being created. But in that case, an email is just sent
  // twice so not a huge deal.
  $nid = 1 + db_result(db_query("SELECT nid FROM {node} ORDER BY nid DESC LIMIT 1"));
  
  // Log that comment came from email so mailnode_phpmailer doesn't send an email as well.
  mailnode_log_email_sent('email', $nid);
  
  # create the new discussion node
  $node->title = $email['headers']['subject'];
  $node->uid = $email['userid'];
  $node->created = time();
  $node->status = 1; # published
  $node->promote = 0;
  $node->sticky = 0;
  $node->body = $mailbody;
  $node->teaser = node_teaser($mailbody);
  $node->type = 'story';
  # TODO: read whether the group is public or not and set og_public accordingly
  $node->og_public = TRUE;
  $node->comment = variable_get("comment_$node_type", COMMENT_NODE_READ_WRITE);
  
  //// Add attachments if any.TODO fix this someday. Best idea -- save mail objects w/ attachments. On cron scoop them up and add them to nodes/comments
  //if (isset($email['attachments'])) {
  //  $nodeattachments = _mailnode_save_attachments_temp_dir($email['attachments']);
  //  $node->mailnode_attachments = $nodeattachments;
  //  _mailnode_save_files($node);
  //}
  //dd_log($node);
  node_save($node);
  
  # save the ancestry (puts it in a group)
  $ancestry = array(
    'nid' => $node->nid,
    'group_nid' => $email['groupid'],
    'is_public' => $node->og_public,
  );
  drupal_write_record('og_ancestry', $ancestry);
  
  $node = node_load($node->nid, null, true);
  
  // Send off email.
  _mailnode_email_node_email($email, $node);
  
  # save a message to the mail log
  echo t("Posted discussion for $mailfrom to group $mail_username with nid=$node->nid.");
}

function _mailnode_email_node_email($email, $node) {
  // Load the space.
  $space = spaces_load('og', $email['groupid']);
  
  // Build new email.
  $email = _mailnode_rewrite_headers($email, $node, $space, true);
  $footer = _mailnode_build_footer($space, $node);
  $email = _mailnode_add_footer($email, $footer);
  $email['new_email_text'] = _mailnode_encode_email(array($email['structure']));
  
  // Send it off.
  _mailnode_send_raw_email($email['new_email_text']);
  mailnode_log_email_sent('email', $node->nid);
}

function _mailnode_email_comment_email($email, $node, $comment) {
  // Load the space.
  $space = spaces_load('og', $email['groupid']);
  
  // Build new email.
  $email = _mailnode_rewrite_headers($email, $node, $space);
  $footer = _mailnode_build_footer($space, $node);
  $email = _mailnode_add_footer($email, $footer);
  $email['new_email_text'] = _mailnode_encode_email(array($email['structure']));
  
  // Send it off.
  _mailnode_send_raw_email($email['new_email_text']);
}

function _mailnode_parse_email($email) {
  $params['include_bodies'] = true; 
  $params['decode_bodies'] = true; 
  $params['decode_headers'] = true; 
  $params['input'] = $email['original_email_text'];
  
  // do the decode
  $email['structure'] = clone $structure = Mail_mimeDecode::decode($params);

  // Pull out attachments (if any) first as querypath doesn't like binary bits
  // it seems.
  foreach ($structure->parts as &$part) { 
    // Check if attachment then add to new email.
    if (isset($part->disposition) and ($part->disposition==='attachment')) {
      $info['data'] = $part->body;
      $info['filemime'] = $part->ctype_primary . "/" . $part->ctype_secondary;
      $info['filename'] = $part->ctype_parameters['name'];
      $email['attachments'][] = $info;
      $part = "";
    }
  }
  
  // Copy headers to $email array.
  $email['headers'] = array_copy($structure->headers);
  
  $xml = Mail_mimeDecode::getXML($structure);
  
  // QueryPath requires text be utf-8.
  $xml = @iconv($email['char_set'], 'utf-8//TRANSLIT', $xml);
  
  // Initialize the QueryPath object.
  $qp = qp($xml);
  
  // Find the text/html body.
  $email['text_html'] = $qp->top()
    ->find("headervalue:contains(text/html)")
    ->parent()
    ->next("body")
    ->text();
  
  // Find the text/plain body. We don't need to worry about plain text attachments
  // as they also are "text/plain" as they were removed earlier.
  $email['text_plain'] = $qp->top()
  ->find("headervalue:contains(text/plain")
  ->parent()
  ->next("body")
  ->text();
  
  $email['text_html'] = html_entity_decode($email['text_html'], ENT_QUOTES);
  $email['text_plain'] = html_entity_decode($email['text_plain'], ENT_QUOTES);
  $email['headers']['subject'] = html_entity_decode(
                                      $email['headers']['subject'], ENT_QUOTES);

  //dd_log("===BODY TEXT/PLAIN===\n" . $email['text_plain']);
  //dd_log("===BODY TEXT/HTML===\n" . $email['text_html']);

  // Move the html version (if available) text version to mailbody.
  $email['mailbody'] = $email['text_plain'];
  $email['isHTML'] = false;
 
  // Save copy of the original mailbody
  $email['orig_mailbody'] = $email['mailbody']; 
 
  return $email;  
}

function _mailnode_save_files(&$node) {
  dd_log("inside save file(s)");
  global $user;
  $user = user_load(array('uid' => $node->uid));
  dd_log('user: ' . $user->uid);
  // If $node->mailnode_attachments is empty or upload not installed just return
  if (!$node->mailnode_attachments || !module_exists('upload')) {
    return;
  }

  // If user doesn't have upload permission then don't bother processing
  // TODO check comment upload permissions.
  if (!(user_access('upload files'))) {
    echo "didn't have permissions?\n\n";
    return;
  }
  
  dd_log($node->mailnode_attachments);
  
  // Convert $node->mailnode_attachments to $node->files ready for upload to use
  foreach ($node->mailnode_attachments as $filekey => $attachment) {
  
    $limits = _upload_file_limits($user);
    $validators = array(
      'file_validate_extensions' => array($limits['extensions']),
      'file_validate_image_resolution' => array($limits['resolution']),
      'file_validate_size' => array($limits['file_size'], $limits['user_size']),
    );
    
    if ($file = _mailnode_save_file($attachment, $validators)) {
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

  // Destroy $node->mailnode_attachments now we have created $node->files
  unset($node->mailnode_attachments);

}


// This started as a copy of file_save_upload
//function _mailnode_node_file($attachment, $source, $validators = array(), $dest = FALSE, $replace = FILE_EXISTS_RENAME) {
function _mailnode_save_file($attachment, $validators = array()) {
  dd_log("inside save file");
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
  dd_log($file);
  dd_log($errors);
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

function _mailnode_save_attachments_temp_dir($attachments) {
  dd_log("inside save attachments to temp dir");
  // Parse each mime part in turn
  foreach ($attachments as $info) {
    // Save the data to temporary file
    $temp_file = tempnam(file_directory_temp(), 'mail');
    $filepath = file_save_data($info['data'], $temp_file);
  
    // Add the item to the attachments array, and sanitise filename
    $node_attachments[] = array(
      'filename' => _mailnode_sanitise_filename($info['filename']),
      'filepath' => $filepath,
      'filemime' => strtolower($info['filemime']),
      'filesize' => strlen($info['data']),
    );
  }
  file_save_data("hello world", file_directory_path() . "/temp");
  
  dd_log($node_attachments);
  // Return the attachments
  return $node_attachments;

}

/**
 * Take a raw attachment filename, decode it if necessary, and strip out invalid characters
 * Return a sanitised filename that should be ok for use by modules that want to save the file
 */
function _mailnode_sanitise_filename($filename) {
  dd_log("inside sanatize filename");
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

function _mailnode_create_new_email($email) {
  $structure = clone $email['structure'];
  $structure = _mailnode_rewrite_headers($structure, $email);
  $structure = _mailnode_add_footer($structure, $email);
  $email['new_email_text'] = _mailnode_encode_email(array($structure));
  
  return $email;
}

// Turn structure back into a plain text email using recursion.
function _mailnode_encode_email($structure, $boundary = "", $email = "") {
  foreach($structure as $part) {   
    if (empty($boundary)) {
      $boundary = $part->ctype_parameters['boundary'];
    }
    if (isset($part->parts)) {
      $email .= _mailnode_encode_email_headers($part->headers) . "\n";
      $email .= "--" . $part->ctype_parameters['boundary'] . "\n";
      $email = _mailnode_encode_email($part->parts, $part->ctype_parameters['boundary'], $email);
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
      
      $email .= _mailnode_encode_email_headers($part->headers) . "\n";
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

function _mailnode_encode_email_headers($array) {
  $header = "";
  foreach ($array as $key => $value) {
    // We remove quoted-printable as content-transfer-encoding
    // because mime_decode decodes that and PHP doesn't have a function
    // AFAIK to reencode the text.
    if ($value && $value !== "quoted-printable") { 
      $header .= capitalizeWords($key, " -") . ": " . $value . "\n";  
    }
  }
  
  return $header;
}

// Keep mime-version, date, subject, from, to, and content-type
function _mailnode_rewrite_headers($email, $node, $space, $new_node = FALSE) {
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
  $new_headers['List-Id'] = "<" . $space->purl . "@island.byu.edu>";
  $new_headers['List-Post'] = "<mailto:" . $space->purl . "@island.byu.edu>";
  $new_headers['List-Archive'] = url("node/" . $space->sid, array('absolute' => TRUE));
  
  // Thread-URL header.
  global $base_url;
  $new_headers['X-Thread-Url'] = $base_url . "/node/" . $node->nid;
  
  // Message-Id We use this to match new comments to their node.
  $new_headers['Message-ID'] = $base_url . "/node/" . $node->nid;
  
  $email['structure']->headers = $new_headers;
  
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
  
  $email['structure'] = $structure;
  
  return $email;
}

function _mailnode_send_raw_email($email_text) {
  $rand_str = rand(1000, 10000);
  write_string_to_file($email_text, $rand_str);
  system("/usr/sbin/exim4 -t < /tmp/" . $rand_str);
}