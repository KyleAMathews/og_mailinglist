<?php
// $Id$

/*
 * @param int $sid Space id to query for subscriptions
 * @param string $space_type Space type e.g. og
 * 
 * @return array of UIDs and email addresses that are subscribed to this thread.
 *  Format = key (UID) => value (email address)
 */
function og_mailinglist_get_space_subscriptions($sid, $space_type) {
  $result = db_query("SELECT uid
                     FROM {og_mailinglist_subscription}
                     WHERE sid = %d
                     And space_type = '%s'
                     AND subscription_type = 'email'", $sid, $space_type);
  
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
                            WHERE sid = %d
                            AND uid = %d", $gid, $uid));
}

/*
 * @param int $sid Space id.
 * @param string $space_type Space type e.g. og.
 * @param array $uids array of User IDs to add group subscriptions.
 */
function og_mailinglist_save_group_subscriptions($sid, $space_type, $uids) {
  foreach ($uids as $uid) {
    db_query("INSERT INTO {og_mailinglist_subscription}
             VALUES (%d, '%s', %d, 'email')", $sid, $space_type, $uid);
  }
}

/*
 * @param int $sid Space id.
 * @param string $space_type Space type e.g. og.
 * @param array $uids array of User IDs to add group subscriptions.
 */
function og_mailinglist_delete_group_subscriptions($sid, $space_type, $uids) {
  foreach ($uids as $uid) {
    // Delete subscription.
    db_query("DELETE FROM {og_mailinglist_subscription}
             WHERE sid = %d
             AND space_type = '%s'
             AND uid = %d", $sid, $space_type, $uid);
    
    og_mailinglist_delete_group_threads($sid, $uid);
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