/*
 * ClutterMozHeadless; A headless Mozilla renderer
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

#ifndef _CLUTTER_MOZHEADLESS_H
#define _CLUTTER_MOZHEADLESS_H

#include <glib-object.h>
#include <gio/gio.h>
#include <moz-headless.h>
#include "clutter-mozembed-comms.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_MOZHEADLESS clutter_mozheadless_get_type()

#define CLUTTER_MOZHEADLESS(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  CLUTTER_TYPE_MOZHEADLESS, ClutterMozHeadless))

#define CLUTTER_MOZHEADLESS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  CLUTTER_TYPE_MOZHEADLESS, ClutterMozHeadlessClass))

#define CLUTTER_IS_MOZHEADLESS(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  CLUTTER_TYPE_MOZHEADLESS))

#define CLUTTER_IS_MOZHEADLESS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  CLUTTER_TYPE_MOZHEADLESS))

#define CLUTTER_MOZHEADLESS_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  CLUTTER_TYPE_MOZHEADLESS, ClutterMozHeadlessClass))

typedef struct _ClutterMozHeadlessPrivate ClutterMozHeadlessPrivate;

typedef struct {
  MozHeadless parent;

  ClutterMozHeadlessPrivate *priv;
} ClutterMozHeadless;

typedef struct
{
  ClutterMozHeadless *parent;

  gchar           *input_file;
  gchar           *output_file;
  GIOChannel      *input;
  GIOChannel      *output;
  guint            watch_id;
  GFileMonitor    *monitor;
  gboolean         waiting_for_ack;
  guint            mack_source;
  guint            sack_source;
} ClutterMozHeadlessView;

typedef struct {
  MozHeadlessClass parent_class;

  /* Signals */
  void (* cancel_download) (ClutterMozHeadless *headless, gint id);
  void (* create_download) (ClutterMozHeadless *headless,
                            const gchar        *uri,
                            const gchar        *target);
} ClutterMozHeadlessClass;

GType clutter_mozheadless_get_type (void);

void
send_feedback_all (ClutterMozHeadless      *headless,
                   ClutterMozEmbedFeedback  feedback,
                   ...);

G_END_DECLS

#endif /* _CLUTTER_MOZHEADLESS_H */

