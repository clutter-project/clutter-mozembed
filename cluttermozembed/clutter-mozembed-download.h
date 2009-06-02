/*
 * ClutterMozembed; a ClutterActor that embeds Mozilla
 * Copyright (c) 2009, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Authored by Chris Lord <chris@linux.intel.com>
 */

#ifndef _CLUTTER_MOZEMBED_DOWNLOAD
#define _CLUTTER_MOZEMBED_DOWNLOAD

#include <glib-object.h>
#include "clutter-mozembed.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_MOZEMBED_DOWNLOAD clutter_mozembed_download_get_type()

#define CLUTTER_MOZEMBED_DOWNLOAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  CLUTTER_TYPE_MOZEMBED_DOWNLOAD, ClutterMozEmbedDownload))

#define CLUTTER_MOZEMBED_DOWNLOAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  CLUTTER_TYPE_MOZEMBED_DOWNLOAD, ClutterMozEmbedDownloadClass))

#define CLUTTER_IS_MOZEMBED_DOWNLOAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  CLUTTER_TYPE_MOZEMBED_DOWNLOAD))

#define CLUTTER_IS_MOZEMBED_DOWNLOAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  CLUTTER_TYPE_MOZEMBED_DOWNLOAD))

#define CLUTTER_MOZEMBED_DOWNLOAD_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  CLUTTER_TYPE_MOZEMBED_DOWNLOAD, ClutterMozEmbedDownloadClass))

typedef struct _ClutterMozEmbedDownloadPrivate ClutterMozEmbedDownloadPrivate;

typedef struct {
  GObject parent;

  ClutterMozEmbedDownloadPrivate *priv;
} ClutterMozEmbedDownload;

typedef struct {
  GObjectClass parent_class;

  /* Signals */
  void (* complete)     (ClutterMozEmbedDownload *download);
} ClutterMozEmbedDownloadClass;

GType clutter_mozembed_download_get_type (void);

const gchar *
clutter_mozembed_download_get_source (ClutterMozEmbedDownload *self);

const gchar *
clutter_mozembed_download_get_destination (ClutterMozEmbedDownload *self);

gdouble
clutter_mozembed_download_get_progress (ClutterMozEmbedDownload *self);

G_END_DECLS

#endif /* _CLUTTER_MOZEMBED_DOWNLOAD */

