# Image Style Warmer

Backdrop normally creates an image style derivative the first time a page
requests it. This module creates those derivatives ahead of time, so visitors
do not have to wait for image processing on the first request.

It works with Backdrop image styles and permanent managed image files. It uses
Backdrop's image APIs and stream wrappers, so it supports public files,
private files, and remote storage such as S3 through the `s3fs` module.

## Configuration

Go to **Configuration > Media > Image Style Warmer**:

`admin/config/media/image-style-warmer`

The page lists every image style and provides three settings.

### Generate immediately on upload

When a new image becomes permanent, the selected styles are generated during
the same request that saves the image.

Use this for a small number of important styles, such as the thumbnail shown
immediately after an upload. The image upload takes longer because the image
processing happens before the request finishes, but the derivative is ready as
soon as the upload completes.

### Generate in the background on upload

When a new image becomes permanent, the selected styles are added to the
`image_style_warmer_derivatives` queue. Backdrop cron processes that queue for
up to 60 seconds per cron run.

Use this for larger images, many styles, slower storage, or styles that do not
need to be available immediately. The upload stays responsive, but the
derivative is not guaranteed to exist until cron has processed the queue.

If cron is not running, queued derivatives remain pending. They can be run
manually with:

```bash
bee image_style_queue_run
```

A style selected for immediate warming is automatically removed from the
queued set for that upload, preventing duplicate work.

### Skip existing derivatives

This is enabled by default. Rebuild operations check whether the derivative
already exists and leave it alone when it does.

Disable this setting to make the default rebuild behavior regenerate existing
derivatives. Individual rebuild operations also provide a **Force
regeneration** checkbox, and the command-line rebuild supports `--force`.

## Rebuilding existing images

The image styles page has a **Rebuild** link beside each style:

`admin/config/media/image-styles`

There is also a **Rebuild all styles** action. Rebuilds scan permanent image
files and use a batch process so large libraries can be handled over multiple
requests. The batch processes 20 files at a time for each style and reports
processed and failed files when it finishes.

The command-line equivalent is:

```bash
# Rebuild every image style.
bee image_style_rebuild

# Rebuild one style by machine name.
bee image_style_rebuild --style=thumbnail

# Regenerate derivatives even when they already exist.
bee image_style_rebuild --force
bee image_style_rebuild --style=thumbnail --force
```

The command is also available as `bee isr`.

## Processing the background queue manually

The queue runner processes items that were created by the background upload
setting or by the bulk action:

```bash
# Process for up to 60 seconds with no item limit.
bee image_style_queue_run

# Process for up to two minutes.
bee image_style_queue_run --time=120

# Process at most 500 items.
bee image_style_queue_run --limit=500

# Apply both limits.
bee isqr --time=120 --limit=500
```

If derivative creation fails, the item remains available for a later retry.
The command reports the number of processed and failed items. A missing image
style is logged and discarded because it represents stale configuration.

## Warming selected existing files

The module provides a file action named **Warm configured image styles**.
On a file listing or View that supports Backdrop bulk actions, select one or
more image files and apply that action. It uses the configured immediate and
background style lists:

- Styles configured for immediate warming are generated while the action runs.
- Styles configured for background warming are added to the queue.

This is useful after importing images, before launching a new image-heavy
page, or when only a selected group of files needs to be prepared.

## What counts as an upload

The module warms only permanent files whose MIME type starts with `image/`.
It handles both direct permanent file saves and the common managed-file
workflow where an uploaded file is temporary first and becomes permanent when
the parent content is saved.

## Permissions

Configuration and rebuild pages require the **Administer image styles**
permission. The bulk warming action uses the same permission.

## Current limitation

The module does not automatically re-warm derivatives when Manual Crop crop
coordinates change. Manual Crop saves crop selections in its own submit
handler and does not expose a reliable crop-change hook in this Backdrop
version. Use the rebuild action or the bulk warming action after changing
existing crop selections.

## Requirements

This module requires that the following module is also enabled:

- Image module


## Installation

- Install this module using the official Backdrop CMS instructions at
  https://docs.backdropcms.org/documentation/extend-with-modules.


## Issues

Bugs and Feature Requests should be reported in the Issue Queue:
https://github.com/backdrop-contrib/image_style_warmer/issues.


## Current Maintainers

[Justin Keiser](https://github.com/keiserjb)


## Credits

- Inspired by the Drupal module of the same name,
  [https://www.drupal.org/project/image_style_warmer](https://www.drupal.org/project/image_style_warmer).
 - Developed with AI assistance.


## License

This project is GPL v2 software.
See the LICENSE.txt file in this directory for complete text.
