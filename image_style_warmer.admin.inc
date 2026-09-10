<?php
/**
 * @file
 * Admin settings and rebuild forms for image_style_warmer.
 */

/**
 * Settings form.
 */
function image_style_warmer_settings_form($form, &$form_state) {
  $config = config('image_style_warmer.settings');
  $options = image_style_options(FALSE);

  if (empty($options)) {
    backdrop_set_message(t('No image styles exist yet. Add one at <a href="@url">Admin &rsaquo; Configuration &rsaquo; Media &rsaquo; Image styles</a> first.', array(
      '@url' => url('admin/config/media/image-styles'),
    )), 'warning');
  }

  $form['warm_on_upload'] = array(
    '#type' => 'checkboxes',
    '#title' => t('Generate immediately on upload'),
    '#description' => t('Selected styles are generated as soon as a permanent image file is saved, before the derivative is ever requested. Best for a small number of styles that must never show a generation delay to a visitor.'),
    '#options' => $options,
    '#default_value' => $config->get('warm_on_upload') ?: array(),
  );

  $form['warm_via_queue'] = array(
    '#type' => 'checkboxes',
    '#title' => t('Generate in the background on upload'),
    '#description' => t('Selected styles are queued and generated during cron instead of blocking the upload request. Best for sites with many styles, large images, or slower storage such as S3.'),
    '#options' => $options,
    '#default_value' => $config->get('warm_via_queue') ?: array(),
  );

  $form['skip_existing'] = array(
    '#type' => 'checkbox',
    '#title' => t('Skip derivatives that already exist when rebuilding'),
    '#description' => t('Uncheck to force the "Rebuild" action to regenerate every derivative, even ones already on disk (or already in the bucket).'),
    '#default_value' => $config->get('skip_existing') !== FALSE,
  );

  $form['actions'] = array('#type' => 'actions');
  $form['actions']['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Save configuration'),
  );

  return $form;
}

/**
 * Submit handler for image_style_warmer_settings_form().
 */
function image_style_warmer_settings_form_submit($form, &$form_state) {
  $config = config('image_style_warmer.settings');
  $config->set('warm_on_upload', array_values(array_filter($form_state['values']['warm_on_upload'])));
  $config->set('warm_via_queue', array_values(array_filter($form_state['values']['warm_via_queue'])));
  $config->set('skip_existing', (bool) $form_state['values']['skip_existing']);
  $config->save();

  backdrop_set_message(t('The configuration options have been saved.'));
}

/**
 * Confirm form for rebuilding one style, or every style.
 *
 * @see image_style_warmer_rebuild_form_submit()
 */
function image_style_warmer_rebuild_form($form, &$form_state, $style = NULL) {
  $skip_existing = config_get('image_style_warmer.settings', 'skip_existing') !== FALSE;

  if (isset($style)) {
    $form['style_name'] = array('#type' => 'value', '#value' => $style['name']);
    $question = t('Rebuild the %style image style?', array('%style' => $style['label']));
  }
  else {
    $form['style_name'] = array('#type' => 'value', '#value' => NULL);
    $question = t('Rebuild every image style?');
  }

  $form['force'] = array(
    '#type' => 'checkbox',
    '#title' => t('Force regeneration of derivatives that already exist'),
    '#default_value' => !$skip_existing,
  );

  $description = t('This regenerates image style derivatives for every permanent image file, using a batch process. It works with whichever storage (stream wrapper) each file actually uses.');
  $path = 'admin/config/media/image-styles';

  return confirm_form($form, $question, $path, $description, t('Rebuild'), t('Cancel'));
}

/**
 * Submit handler for image_style_warmer_rebuild_form().
 */
function image_style_warmer_rebuild_form_submit($form, &$form_state) {
  $style_name = $form_state['values']['style_name'];
  $force = !empty($form_state['values']['force']);

  $styles = $style_name ? array(image_style_load($style_name)) : image_styles();

  $operations = array();
  foreach ($styles as $style) {
    if ($style) {
      $operations[] = array('image_style_warmer_rebuild_batch_op', array($style, $force));
    }
  }

  if (empty($operations)) {
    backdrop_set_message(t('No image style to rebuild.'), 'warning');
    return;
  }

  $batch = array(
    'title' => t('Rebuilding image style derivatives'),
    'operations' => $operations,
    'finished' => 'image_style_warmer_rebuild_batch_finished',
    'file' => backdrop_get_path('module', 'image_style_warmer') . '/image_style_warmer.admin.inc',
  );
  batch_set($batch);
}

/**
 * Batch operation: rebuild derivatives for one style, a page of files at a
 * time.
 */
function image_style_warmer_rebuild_batch_op($style, $force, &$context) {
  if (!isset($context['sandbox']['progress'])) {
    $context['sandbox']['progress'] = 0;
    $context['sandbox']['last_fid'] = 0;
    $context['sandbox']['max'] = (int) db_query("SELECT COUNT(*) FROM {file_managed} WHERE status = :status AND filemime LIKE :mime", array(
      ':status' => FILE_STATUS_PERMANENT,
      ':mime' => 'image/%',
    ))->fetchField();
    $context['results']['processed'] = isset($context['results']['processed']) ? $context['results']['processed'] : 0;
    $context['results']['failed'] = isset($context['results']['failed']) ? $context['results']['failed'] : 0;
  }

  if ($context['sandbox']['max'] === 0) {
    $context['finished'] = 1;
    return;
  }

  $limit = 20;
  $result = db_query_range("SELECT fid, uri FROM {file_managed} WHERE status = :status AND filemime LIKE :mime AND fid > :last_fid ORDER BY fid ASC", 0, $limit, array(
    ':status' => FILE_STATUS_PERMANENT,
    ':mime' => 'image/%',
    ':last_fid' => $context['sandbox']['last_fid'],
  ));

  $count = 0;
  foreach ($result as $row) {
    $count++;
    $context['sandbox']['last_fid'] = $row->fid;
    $context['sandbox']['progress']++;
    if (image_style_warmer_create_derivative($style, $row->uri, $force)) {
      $context['results']['processed']++;
    }
    else {
      $context['results']['failed']++;
    }
  }

  $context['message'] = t('Rebuilding %style: @done of @total files.', array(
    '%style' => $style['label'],
    '@done' => $context['sandbox']['progress'],
    '@total' => $context['sandbox']['max'],
  ));

  $context['finished'] = ($count < $limit || $context['sandbox']['progress'] >= $context['sandbox']['max']) ? 1 : ($context['sandbox']['progress'] / $context['sandbox']['max']);
}

/**
 * Batch 'finished' callback for the rebuild batch.
 */
function image_style_warmer_rebuild_batch_finished($success, $results, $operations) {
  if ($success) {
    backdrop_set_message(t('Rebuild complete: @processed derivative(s) processed, @failed failed.', array(
      '@processed' => isset($results['processed']) ? $results['processed'] : 0,
      '@failed' => isset($results['failed']) ? $results['failed'] : 0,
    )), empty($results['failed']) ? 'status' : 'warning');
  }
  else {
    backdrop_set_message(t('An error occurred while rebuilding image style derivatives.'), 'error');
  }
}
