<?php
/**
 * @file
 * Bee command for rebuilding image style derivatives from the command line.
 */

/**
 * Implements hook_bee_command().
 */
function image_style_warmer_bee_command() {
  $items = array();

  $items['image_style_rebuild'] = array(
    'description' => bt('Rebuild image style derivatives for one style, or every style.'),
    'callback' => 'bee_image_style_warmer_image_style_rebuild',
    'aliases' => array('isr'),
    'bootstrap' => BEE_BOOTSTRAP_FULL,
    'options' => array(
      'style' => array(
        'description' => bt('Machine name of a single image style to rebuild. Omit to rebuild every style.'),
        'value' => 'optional',
      ),
      'force' => array(
        'description' => bt('Regenerate derivatives even if they already exist.'),
        'value' => 'optional',
      ),
      'limit' => array(
        'description' => bt('Maximum number of image files to process for each style. Zero means no item limit.'),
        'value' => 'optional',
      ),
    ),
  );

  $items['image_style_queue_run'] = array(
    'description' => bt('Process queued image style derivatives.'),
    'callback' => 'bee_image_style_warmer_image_style_queue_run',
    'aliases' => array('isqr'),
    'bootstrap' => BEE_BOOTSTRAP_FULL,
    'options' => array(
      'time' => array(
        'description' => bt('Maximum seconds to spend processing the queue. Defaults to 60.'),
        'value' => 'optional',
      ),
      'limit' => array(
        'description' => bt('Maximum number of queue items to process. Zero means no item limit.'),
        'value' => 'optional',
      ),
    ),
  );

  return $items;
}

/**
 * Validates bee_image_style_warmer_image_style_rebuild().
 */
function bee_image_style_warmer_image_style_rebuild_validate($arguments = array(), $options = array()) {
  $style_name = !empty($options['style']) ? $options['style'] : NULL;
  if ($style_name && !image_style_load($style_name)) {
    return bee_set_error('IMAGE_STYLE_WARMER_UNKNOWN_STYLE', bt('Unknown image style: @style', array('@style' => $style_name)));
  }
}

/**
 * Rebuilds image style derivatives from the command line.
 */
function bee_image_style_warmer_image_style_rebuild($arguments = array(), $options = array()) {
  $style_name = !empty($options['style']) ? $options['style'] : NULL;
  $force = !empty($options['force']);
  $limit = isset($options['limit']) ? max(0, (int) $options['limit']) : 0;

  if ($style_name && !image_style_load($style_name)) {
    bee_message(bt('Unknown image style: @style', array('@style' => $style_name)), 'error');
    return;
  }

  $styles = $style_name ? array(image_style_load($style_name)) : image_styles();

  foreach ($styles as $style) {
    if (!$style) {
      continue;
    }

    $processed = 0;
    $failed = 0;
    $last_fid = 0;

    do {
      $range = $limit ? min(50, $limit - $processed - $failed) : 50;
      if ($range <= 0) {
        break;
      }
      $result = db_query_range("SELECT fid, uri FROM {file_managed} WHERE status = :status AND filemime LIKE :mime AND fid > :last_fid ORDER BY fid ASC", 0, $range, array(
        ':status' => FILE_STATUS_PERMANENT,
        ':mime' => 'image/%',
        ':last_fid' => $last_fid,
      ));

      $count = 0;
      foreach ($result as $row) {
        $count++;
        $last_fid = $row->fid;
        if (image_style_warmer_create_derivative($style, $row->uri, $force)) {
          $processed++;
        }
        else {
          $failed++;
        }
      }
    } while ($count > 0 && (!$limit || $processed + $failed < $limit));

    bee_message(bt('@style: @processed processed, @failed failed.', array(
      '@style' => $style['label'],
      '@processed' => $processed,
      '@failed' => $failed,
    )));
  }
}

/**
 * Processes queued image style derivatives from the command line.
 */
function bee_image_style_warmer_image_style_queue_run($arguments = array(), $options = array()) {
  $time = isset($options['time']) ? $options['time'] : NULL;
  $time = ($time === NULL || $time === '') ? 60 : max(1, (int) $time);
  $limit = isset($options['limit']) ? $options['limit'] : NULL;
  $limit = ($limit === NULL || $limit === '') ? 0 : max(0, (int) $limit);

  $queue = BackdropQueue::get('image_style_warmer_derivatives');
  $end = time() + $time;
  $processed = 0;
  $failed = 0;

  while (time() < $end && (!$limit || $processed + $failed < $limit) && ($item = $queue->claimItem())) {
    try {
      image_style_warmer_process_queue_item($item->data);
      $queue->deleteItem($item);
      $processed++;
    }
    catch (Exception $e) {
      $queue->releaseItem($item);
      $failed++;
      bee_message(bt('Queue item failed: @message', array('@message' => $e->getMessage())), 'error');
      // Do not immediately reclaim the same released item in this run.
      break;
    }
  }

  bee_message(bt('Queue run complete: @processed processed, @failed failed.', array(
    '@processed' => $processed,
    '@failed' => $failed,
  )));
}
