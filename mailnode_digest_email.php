<?php

# boostrap drupal
# set up the drupal directory -- very important 
$DRUPAL_DIR = '/home/kyle/workspace/www/edully/';

# set some server variables so Drupal doesn't freak out
$_SERVER['SCRIPT_NAME'] = '/script.php';
$_SERVER['SCRIPT_FILENAME'] = '/script.php';
$_SERVER['HTTP_HOST'] = 'example.com';
$_SERVER['REMOTE_ADDR'] = '127.0.0.1';
$_SERVER['REQUEST_METHOD'] = 'POST';

# act as the first user
global $user;
$user->uid = 1;

# gain access to drupal
chdir($DRUPAL_DIR);  # drupal assumes this is the root dir
error_reporting(E_ERROR | E_PARSE); # drupal throws a few warnings, so suppress these
require_once('./includes/bootstrap.inc');
drupal_bootstrap(DRUPAL_BOOTSTRAP_FULL);

# restore error reporting to its normal setting
error_reporting(E_ALL);

// Get list of groups where at least one person has subscribed to a digest node
// and which had a new post or comment in the last 24 hours.

$digest_day = 8640000;

$new_nodes_sql = 'SELECT DISTINCT s.sid
          FROM {mailnode_subscription} s
          JOIN {og_ancestry} o
          JOIN {node} n
          WHERE s.sid = o.group_nid
          AND o.nid = n.nid
          AND n.created > (unix_timestamp() - %d)
          AND s.subscription_type = "digest email"';
          
$new_comments_sql = 'SELECT DISTINCT s.sid
          FROM {mailnode_subscription} s
          JOIN {og_ancestry} o
          JOIN {comments} c
          WHERE s.sid = o.group_nid
          AND o.nid = c.nid
          AND c.timestamp > (unix_timestamp() - %d)
          AND s.subscription_type = "digest email"';

$groups_with_new_nodes = db_query($new_nodes_sql, $digest_day);
$groups_with_new_comments = db_query($new_comments_sql, $digest_day);

$digest_groups = array();
while ($data = db_fetch_array($groups_with_new_comments)) {
  $digest_groups[$data['sid']] = $data['sid'];
}
while ($data = db_fetch_array($groups_with_new_nodes)) {
  $digest_groups[$data['sid']] = $data['sid'];
}

print_r($digest_groups);
foreach ($digest_groups as $gid) {
  // Get list of new activity -- new nodes and new comments
  $new_nids = 'SELECT o.nid
                FROM {node} n
                JOIN {og_ancestry} o
                WHERE n.nid = o.nid
                AND n.created > (unix_timestamp() - %d)
                AND o.group_nid = %d';
  
  $new_comment_nids = 'SELECT DISTINCT c.nid, c.cid
                        FROM {comments} c
                        JOIN {og_ancestry} o
                        WHERE c.nid = o.nid
                        AND c.timestamp > (unix_timestamp() - %d)
                        AND o.group_nid = %d';

  $nids_with_new_nodes = db_query($new_nids, $digest_day, $gid);
  $nids_with_new_comments = db_query($new_comments_nids, $digest_day, $gid);

  $nids = array();
  $cids = array();
  while ($data = db_fetch_array($nids_with_new_comments)) {
    $nids[$data['nid']] = "old";
  }
  while ($data = db_fetch_array($nids_with_new_nodes)) {
    $nids[$data['nid']] = "new";
  }
  
  print_r($gid);
  print_r($nids);
  
  // Assemble message
  $purl_id = db_result(db_query("SELECT value
                                      FROM {purl}
                                      WHERE provider = 'spaces_og'
                                      AND id = %d", $gid));
  
  $subject = "Digest for " . $purl_id . "@" . variable_get("mailnode_server_string", "example.com")
        . " - 8 Messages in " . count($nids) . " Discussions";
  
  print_r($subject);

  // For each person, send out an email. 
}
  
?>