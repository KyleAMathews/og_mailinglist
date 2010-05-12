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
  // Clean bad emails from list.
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
  foreach ($uids as $uid) {
    db_query("INSERT INTO {og_mailinglist_thread}
             VALUES (%d, %d)", $nid, $uid);
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
             VALUES (%d, %d, 'email')", $gid, $uid);
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

function og_mailinglist_delete_group_threads($gid, $uid) {
  // Delete threads.
  db_query("DELETE m FROM {og_mailinglist_thread} m
           INNER JOIN {og_ancestry} o
           WHERE o.nid = m.nid
           AND m.uid = %d
           AND o.group_nid = %d", $uid, $gid);  
}

function og_mailinglist_delete_thread_subscription($nid, $uid) {
  return db_query("DELETE FROM {og_mailinglist_thread}
           WHERE nid = %d
           AND uid = %d", $nid, $uid);
}

/*
 * Log successful sent email.
 */
function og_mailinglist_log_email_sent($source, $nid, $cid = 0) {
  return db_query("INSERT INTO {og_mailinglist_source}
           VALUES (%d, %d, '%s')", $nid, $cid, $source);
}
/*
 * @return PHPMailer object.
 */
function og_mailinglist_create_mailer() {
  require_once("phpmailer/class.phpmailer.php");
  $mailer = new PHPMailer();
  $mailer->CharSet = 'UTF-8';

  return $mailer;
}

/*
 * Save group information upon group creation and update.
 */
function og_mailinglist_save_group($nid, $group_email) {
  $data = array(
      'nid' => $nid,
      'group_email' => $group_email,
  );

  drupal_write_record('og_mailinglist', $data);
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
