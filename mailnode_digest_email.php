<?php

# boostrap drupal
# set up the drupal directory -- very important 
$DRUPAL_DIR = '/var/www/island_prod';
require_once('mailnode_utilities.inc');

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

$digest_day = 86400;

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

//print_r($digest_groups);
foreach ($digest_groups as $gid) {
  // Get list of new activity -- new nodes and new comments
  $new_nids = 'SELECT o.nid
                FROM {node} n
                JOIN {og_ancestry} o
                WHERE n.nid = o.nid
                AND n.created > (unix_timestamp() - %d)
                AND o.group_nid = %d';
  
  $new_comment_nids = 'SELECT c.nid, c.cid
                        FROM {comments} c
                        JOIN {og_ancestry} o
                        WHERE c.nid = o.nid
                        AND c.timestamp > (unix_timestamp() - %d)
                        AND o.group_nid = %d';

  $nids_with_new_nodes = db_query($new_nids, $digest_day, $gid);
  $nids_with_new_comments = db_query($new_comment_nids, $digest_day, $gid);

  $nids = array();
  while ($data = db_fetch_array($nids_with_new_comments)) {
    $nids[$data['nid']]['status'] = "old";
    $nids[$data['nid']]['node_obj'] = node_load(array("nid" => $data['nid']));
    $nids[$data['nid']][$data['cid']] = _comment_load($data['cid']);
  }
  while ($data = db_fetch_array($nids_with_new_nodes)) {
    $nids[$data['nid']]['status'] = "new";
    $nids[$data['nid']]['node_obj'] = node_load(array("nid" => $data['nid']));
  }
  
  
  
  // Count # of messages.
  $message_count = 0;
  foreach ($nids as $nid) {
    if ($nid['status'] === "new") {
      $message_count++;
    }
    $message_count += count(array_keys($nid)) - 1;
  }
  
  $purl_id = db_result(db_query("SELECT value
                                      FROM {purl}
                                      WHERE provider = 'spaces_og'
                                      AND id = %d", $gid));
  
  $subject = "Digest for " . $purl_id . "@" . variable_get("mailnode_server_string", "example.com")
        . " - " . $message_count . " Messages in " . count($nids) . " Discussions";
  
  echo $subject . "\n\n";
  
  // Assemble message
  $body = "";
  $body .= "<h2>Today's Discussion Summary</h2>\n";
  $body .= "Group: " . url("node/" . $gid, array('absolute' => TRUE)) . "\n";
  $body .= "<ul>\n";
  foreach ($nids as $nid) {
    $body .= "<li>" . l($nid['node_obj']->title, "node/" . $nid['node_obj']->nid,
                         array('absolute' => TRUE)) . "</li>\n";
  }
  $body .= "</ul>\n";
  $body .= "<hr />\n";

  // Add individual discussions
  foreach ($nids as $nid) {
    $body .= "<h3>Discussion: " . l($nid['node_obj']->title, "node/" .
              $nid['node_obj']->nid, array('absolute' => TRUE)) . "</h3>\n";
    
    // If new node created today.
    if ($nid['status'] === "new") {
      $body .= mailnode_style_node_message($nid['node_obj']);
    }
    
    foreach ($nid as $cid => $comment) {
      if (is_numeric($cid)) {
        $body .= mailnode_style_comment_message($comment, $nid['node_obj']);
      }
    }
  }
  
  echo $body;
  echo "\n\n\n\n";
 
  // For each person, send out an email.
  if ($gid == 223) {
    $mailer = mailnode_create_mailer();
    $mailer->From = "no_reply@island.byu.edu";
    $mailer->AddAddress("mathews.kyle@gmail.com");
    $mailer->Subject = $subject;
    $mailer->Body = $body;
    $mailer->isHTML(TRUE);
    echo "SENDING EMAIL";
    echo $mailer->Send();
  }
}

function mailnode_style_node_message($node) {
  $user = user_load(array('uid' => $node->uid));
  $body .= "<strong>" . $user->name . "</strong> " . $user->mail . " " . $node->creation . "\n";
  $body .= "<br />\n";
  $body .= node_view($node);
  $body .= "<br />\n";
  $body .= "<hr />\n";
  
  return $body;
}

function mailnode_style_comment_message($comment, $node) {
  $user = user_load(array('uid' => $comment->uid));
  $body .= "<strong>" . $user->name . "</strong> " . $user->mail . " " . $comment->timestamp . "\n";
  $body .= "<br />\n";
  $body .= theme_comment_view($comment, $node);
  $body .= "br />\n";
  $body .= "<hr />\n";
  
  return $body;
}
?>