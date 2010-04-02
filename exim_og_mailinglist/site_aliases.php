<?php

/*
 * @return array of sites w/ equivalent drush site aliases.
 */
function og_mailinglist_site_aliases() {
  return array (
    'example.com' => '@example.com',
    'community.example.com' => '@community.example.com',
    'island.byu.edu' => '@dev.island',
  );
}
