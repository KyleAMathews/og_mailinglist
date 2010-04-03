<?php
// $Id: example.drush.inc,v 1.1 2010/03/26 18:48:47 greg1anderson Exp $

/**
 * @file
 * OG Mailinglist Drush commands
 */

/**
 * Implementation of hook_drush_command().
 *
 * In this hook, you specify which commands your
 * drush module makes available, what it does and
 * description.
 *
 * Notice how this structure closely resembles how
 * you define menu hooks.
 *
 * @See drush_parse_command() for a list of recognized keys.
 *
 * @return
 *   An associative array describing your command(s).
 */
function og_mailinglist_drush_command() {
  $items = array();

  // the key in the $items array is the name of the command.
  $items['ogm-group-exists'] = array(
    'callback' => '_og_mailinglist_check_group_exists',
    'description' => "Checks if a group exists, only works with purl at the moment.",
  );
  $items['ogm-post-email'] = array(
    'callback' => '_og_mailinglist_post_email',
    'description' => "Posts email to appropriate group.",
  );

  // more commands here ...

  return $items;
}

/**
 * Implementation of hook_drush_help().
 *
 * This function is called whenever a drush user calls
 * 'drush help <name-of-your-command>'
 *
 * @param
 *   A string with the help section (prepend with 'drush:')
 *
 * @return
 *   A string with the help text for your command.
 */
function example_drush_help($section) {
  switch ($section) {
    case 'drush:example-foo':
      return dt("Prints the amount of time since January 1st, 1970, in years and weeks.");
  }
}

/**
 * ogm-group-exists command callback.
 * @return boolean If group exists or not.
 * @param string purl_group_prefix The email username
 */

function _og_mailinglist_check_group_exists() {
  $args = func_get_args();
  $purl_group_prefix = strtolower($args[0]);
  
  $sql = "SELECT value FROM {purl} WHERE LOWER(value)= '%s'";
  $result = db_result(db_query($sql, $purl_group_prefix));
  
  return $result;
}

/**
 * 
 */
function _og_mailinglist_post_email() {
  // Load file
  require_once(drupal_get_path('module', 'og_mailinglist') . "/og_mailinglist_exim4_transport.php");
  $args = func_get_args();
  $raw_email = $args[0];
  $mail_username = $args[1];
  
  // Remove extra single quotes introduced by shellescapearg()
  $raw_email = trim($raw_email, "' ");
  $raw_email = str_replace("\''", "", $raw_email);

  //echo "raw email: " . $raw_email . "\n";
  echo "we're posting an email to " . $mail_username . "\n";
  _og_mailinglist_process_email($raw_email, $mail_username);
}