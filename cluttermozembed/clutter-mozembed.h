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

#ifndef _CLUTTER_MOZEMBED
#define _CLUTTER_MOZEMBED

#include <glib-object.h>
#include <clutter/clutter.h>
#include <moz-headless.h>
#include "clutter-mozembed-download.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_MOZEMBED clutter_mozembed_get_type()

#define CLUTTER_MOZEMBED(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  CLUTTER_TYPE_MOZEMBED, ClutterMozEmbed))

#define CLUTTER_MOZEMBED_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  CLUTTER_TYPE_MOZEMBED, ClutterMozEmbedClass))

#define CLUTTER_IS_MOZEMBED(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  CLUTTER_TYPE_MOZEMBED))

#define CLUTTER_IS_MOZEMBED_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  CLUTTER_TYPE_MOZEMBED))

#define CLUTTER_MOZEMBED_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  CLUTTER_TYPE_MOZEMBED, ClutterMozEmbedClass))

typedef struct _ClutterMozEmbedPrivate ClutterMozEmbedPrivate;

typedef struct {
  ClutterTexture parent;

  ClutterMozEmbedPrivate *priv;
} ClutterMozEmbed;

typedef struct {
  ClutterTextureClass parent_class;

  /* Signals */
  void (* progress)     (ClutterMozEmbed *mozembed, gdouble progress);
  void (* net_start)    (ClutterMozEmbed *mozembed);
  void (* net_stop)     (ClutterMozEmbed *mozembed);
  void (* crashed)      (ClutterMozEmbed *mozembed);
  void (* new_window)   (ClutterMozEmbed  *mozembed,
                         ClutterMozEmbed **new_mozembed,
                         guint             chromeflags);
  void (* closed)       (ClutterMozEmbed *mozembed);
  void (* link_message) (ClutterMozEmbed *mozembed, const gchar *message);
  void (* size_request) (ClutterMozEmbed *mozembed, gint width, gint height);
  void (* download)     (ClutterMozEmbed         *mozembed,
                         ClutterMozEmbedDownload *download);
} ClutterMozEmbedClass;

GType clutter_mozembed_get_type (void);

ClutterActor *clutter_mozembed_new (void);
ClutterActor *clutter_mozembed_new_with_parent (ClutterMozEmbed *parent);
ClutterActor *clutter_mozembed_new_for_new_window (void);
ClutterActor *clutter_mozembed_new_view (void);

void clutter_mozembed_connect_view (ClutterMozEmbed *mozembed,
                                    const gchar     *input,
                                    const gchar     *output);

void clutter_mozembed_open (ClutterMozEmbed *mozembed, const gchar *uri);
const gchar *clutter_mozembed_get_location (ClutterMozEmbed *mozembed);
const gchar *clutter_mozembed_get_title (ClutterMozEmbed *mozembed);
const gchar *clutter_mozembed_get_icon (ClutterMozEmbed *mozembed);
gboolean clutter_mozembed_can_go_back (ClutterMozEmbed *mozembed);
gboolean clutter_mozembed_can_go_forward (ClutterMozEmbed *mozembed);
void clutter_mozembed_back (ClutterMozEmbed *mozembed);
void clutter_mozembed_forward (ClutterMozEmbed *mozembed);
void clutter_mozembed_stop (ClutterMozEmbed *mozembed);
void clutter_mozembed_refresh (ClutterMozEmbed *mozembed);
void clutter_mozembed_reload (ClutterMozEmbed *mozembed);
GList *clutter_mozembed_get_downloads (ClutterMozEmbed *mozembed);

gboolean clutter_mozembed_get_scrollbars (ClutterMozEmbed *mozembed);
gboolean clutter_mozembed_get_async_scroll (ClutterMozEmbed *mozembed);
void clutter_mozembed_set_scrollbars (ClutterMozEmbed *mozembed, gboolean show);
void clutter_mozembed_set_async_scroll (ClutterMozEmbed *mozembed, gboolean async);
void clutter_mozembed_scroll_by (ClutterMozEmbed *mozembed, gint dx, gint dy);

gboolean clutter_mozembed_is_loading (ClutterMozEmbed *mozembed);
gdouble clutter_mozembed_get_progress (ClutterMozEmbed *mozembed);

MozHeadlessCursorType clutter_mozembed_get_cursor (ClutterMozEmbed *mozembed);

G_END_DECLS

#endif /* _CLUTTER_MOZEMBED */

