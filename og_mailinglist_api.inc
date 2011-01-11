<?php
// $Id$

/**
 * Saves new thread subscriptions for members of a group.
 * 
 * @param int $gid Group ID
 * @param int $nid Node ID
 */
function og_mailinglist_save_group_thread_subscriptions($gid, $nid) {
  $subscribers = og_mailinglist_get_group_subscriptions($gid);
  // Clean bad email addresses from the array.
  foreach ($subscribers as $uid => $email) {
    if (!valid_email_address($email)) {
      unset($subscribers[$uid]);
    }
  }
  // Save users to og_mailinglist_thread
  og_mailinglist_save_thread_subscriptions($nid, array_keys($subscribers));
}
/**
 * Returns users who are active email subscribers in a group.
 *
 * @param int $gid Group id to query for subscriptions
 * 
 * @return array of UIDs and email addresses that are subscribed to this group.
 *  Format = key (UID) => value (email address)
 */
function og_mailinglist_get_group_subscriptions($gid) {
  $result = db_query("SELECT uid
                     FROM {og_mailinglist_subscription}
                     WHERE nid = %d
                     AND subscription_type = 'email'", $gid);
  
  $uids = array();
  while ($data = db_fetch_array($result)) {
    $uids[] = $data['uid'];  
  }
  
  $emails = array();
  foreach ($uids as $uid) {
    $emails[$uid] = db_result(db_query("SELECT mail
                                   FROM {users}
                                   WHERE uid = %d", $uid));
  }
  
  return $emails;
}

/*
 * @return array of UIDs and email addresses that are subscribed to this thread.
 *  Format = key (UID) => value (email address)
 */
function og_mailinglist_get_thread_subscriptions($nid) {
  $result = db_query("SELECT uid
                     FROM {og_mailinglist_thread}
                     WHERE nid = %d", $nid);
  
  $uids = array();
  while ($data = db_fetch_array($result)) {
    $uids[] = $data['uid'];  
  }
  
  $emails = array();
  foreach ($uids as $uid) {
    $emails[$uid] = db_result(db_query("SELECT mail
                                   FROM {users}
                                   WHERE uid = %d", $uid));
  }
  
  return $emails;
}

/*
 * Check if a user is subscribed to a thread.
 */
function og_mailinglist_check_user_subscribed_to_thread($nid, $uid) {
  return db_result(db_query("SELECT uid
          FROM {og_mailinglist_thread
          WHERE nid = %d
          AND uid = %d", $nid, $uid));
}

/*
 * @param int $nid Node id for thread.
 * @param array $uids array of User IDs that are subscribed to this thread.
 *
 * @return null.
 */
function og_mailinglist_save_thread_subscriptions($nid, $uids) {
  static $saved = array();
  foreach ($uids as $uid) {
    // Check that this subscription hasn't already been saved on this page load.
    if (in_array($nid . "_" . $uid, $saved)) {
      continue;
    }
    $saved[] = $nid . "_" . $uid;

    if ($uid != 0) { // Ignore anonymous users.
      $result = db_query("INSERT INTO {og_mailinglist_thread}
             VALUES (%d, %d)", $nid, $uid);
      if ($result) {
        // Invoke hook
        module_invoke_all('og_mailinglist_save_thread_subscription', $nid, $uid);
      }
    }
  }
}

/*
 * @return subscription type for the given group and user.
 */
function og_mailinglist_get_group_subscription_type($gid, $uid) {
  return db_result(db_query("SELECT subscription_type
                            FROM {og_mailinglist_subscription}
                            WHERE nid = %d
                            AND uid = %d", $gid, $uid));
}

/*
 * Update a user's subscription type for one of their groups.
 */
function og_mailinglist_update_group_subcription_type($gid, $uid, $subscription_type) {
  db_query("UPDATE {og_mailinglist_subscription}
         SET subscription_type = '%s'
         WHERE uid = %d
         AND nid = %d", $subscription_type, $uid, $gid);
}

/*
 * @param int $gid Group id.
 * @param array $uids array of User IDs to add group subscriptions.
 */
function og_mailinglist_save_group_subscriptions($gid, $uids) {
  foreach ($uids as $uid) {
    db_query("INSERT INTO {og_mailinglist_subscription}
             VALUES (%d, %d, '%s')", $gid, $uid, variable_get('og_mailinglist_default_group_subscription_type', 'email'));
  }
}

/*
 * @param int $gid Group id.
 * @param array $uids array of User IDs to add group subscriptions.
 */
function og_mailinglist_delete_group_subscriptions($gid, $uids) {
  foreach ($uids as $uid) {
    // Delete subscription.
    db_query("DELETE FROM {og_mailinglist_subscription}
             WHERE nid = %d
             AND uid = %d", $gid, $uid);
    
    og_mailinglist_delete_group_threads($gid, $uid);
  }
}

/*
 * Adds person to 15 latest threads in a group.
 */
function og_mailinglist_subscribe_group_threads($gid, $uid) {
  // Select threads created in the past month.
  $results = db_query("SELECT o.nid
                      FROM {og_ancestry} o
                      INNER JOIN {node} n
                      ON n.nid = o.nid
                      WHERE o.group_nid = %d
                      ORDER BY created DESC
                      LIMIT 15", $gid);
  
  while ($data = db_fetch_array($results)) {
    og_mailinglist_save_thread_subscriptions($data['nid'], array($uid));
  }
}

/**
 * Unsubscribe a user from all group threads when they leave that group.
 **/
function og_mailinglist_delete_group_threads($gid, $uid) {
  // Delete threads.
  db_query("DELETE m FROM {og_mailinglist_thread} m
           INNER JOIN {og_ancestry} o
           WHERE o.nid = m.nid
           AND m.uid = %d
           AND o.group_nid = %d", $uid, $gid);  
}

/**
 * Remove a user from a thread.
 **/
function og_mailinglist_delete_thread_subscription($nid, $uid) {
  static $deleted = array();
  // Check that this subscription hasn't already been deleted.
  if (in_array($nid . "_" . $uid, $deleted)) {
    return;
  }
  $deleted[] = $nid . "_" . $uid;

  $result = db_query("DELETE FROM {og_mailinglist_thread}
           WHERE nid = %d
           AND uid = %d", $nid, $uid);

  if ($result) {
    // Invoke hook
    module_invoke_all('og_mailinglist_delete_thread_subscription', $nid, $uid);
  }
}

/*
 * Log successful sent email.
 */
function og_mailinglist_log_email_sent($source, $nid, $cid = 0, $message_id = 0, $in_reply_to = 0, $references = 0, $parent_message_id = 0) {
  return db_query("INSERT INTO {og_mailinglist_source}
           VALUES (%d, %d, '%s', '%s', '%s', '%s', '%s')", 
           $nid, $cid, $source, $message_id, $in_reply_to, $references, $parent_message_id);
}
/*
 * @return PHPMailer object.
 */
function og_mailinglist_create_mailer() {
  og_mailinglist_phpmailer_load_library();
  $mailer = new PHPMailer(TRUE); // The TRUE param means it will throw exceptions on errors, which we need to catch.
  $mailer->CharSet = 'UTF-8';

  return $mailer;
}

/*
 * Save group information upon group creation and update.
 */
function og_mailinglist_save_group($nid, $group_email) {
  $update = db_result(db_query("SELECT nid
                                FROM {og_mailinglist}
                                WHERE nid = %d", $nid));
  $data = array(
      'nid' => $nid,
      'group_email' => $group_email,
  );

  if ($update) {
    drupal_write_record('og_mailinglist', $data, 'nid');
  }
  else {
    drupal_write_record('og_mailinglist', $data);
  }
}

/*
 * Delete group information.
 */
function og_mailinglist_delete_group($nid) {
  // Delete information from og_mailinglist.
  db_query("DELETE FROM {og_mailinglist}
           WHERE nid = %d", $nid);
  
  // Delete group subscriptions from og_mailinglist_subscriptions.
  db_query("DELETE FROM {og_mailinglist_subscription}
           WHERE nid = %d", $nid);
}

function _og_mailinglist_get_thread_parent_messageid($in_reply_to_message_id, $references) {
  if (empty($in_reply_to_message_id)) {
    return;
  }
  $parent_message_id = db_result(db_query("SELECT parent_message_id
                              FROM {og_mailinglist_source}
                              WHERE message_id = '%s'", $in_reply_to_message_id));

  if (!empty($parent_message_id)) {
    return $parent_message_id;
  }

  // Try to get the parent message-id from one of the message-ids in the references header.
  $references = str_replace("\r\n", "", $references);
  $refs = str_replace("\n", "", $references);
  $refs_array = explode(" ", $refs);

  foreach ($refs_array as $reference) {
    if(!empty($reference)) {
        $parent_message_id = db_result(db_query("SELECT parent_message_id
                              FROM {og_mailinglist_source}
                              WHERE message_id = '%s'", $reference));

        if (!empty($parent_message_id)) {
          return $parent_message_id;
        }
    }
  }
 
}

function _og_mailinglist_get_nid_from_parent_messageid($message_id) {
  return db_result(db_query("SELECT nid
                              FROM {og_mailinglist_source}
                              WHERE message_id = '%s'", $message_id));
}

function _og_mailinglist_get_messageid_from_nid($nid) {
  return db_result(db_query("SELECT parent_message_id
                              FROM {og_mailinglist_source}
                              WHERE cid = 0
                              AND nid = %d", $nid));
}
