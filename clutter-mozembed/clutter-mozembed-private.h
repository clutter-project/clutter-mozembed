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

#ifndef _CLUTTER_MOZEMBED_PRIVATE
#define _CLUTTER_MOZEMBED_PRIVATE

#include <config.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#ifdef SUPPORT_PLUGINS
#include <X11/extensions/Xcomposite.h>
#include <clutter/x11/clutter-x11.h>
#include <clutter/glx/clutter-glx.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#endif

#include "clutter-mozembed.h"
#include "clutter-mozembed-download.h"

#ifdef SUPPORT_IM
#include "clutter-imcontext/clutter-immulticontext.h"
#endif

struct _ClutterMozEmbedPrivate
{
  GFileMonitor    *monitor;
  GIOChannel      *input;
  GIOChannel      *output;
  guint            watch_id;
  GPid             child_pid;

  gchar           *input_file;
  gchar           *output_file;
  gchar           *shm_name;
  gboolean         opened_shm;
  int              shm_fd;
  gboolean         spawn;

  void            *image_data;
  int              image_size;
  guint            repaint_id;

  gboolean         read_only;

  /* Variables for throttling motion events */
  gboolean            motion_ack;
  gboolean            pending_motion;
  gint                motion_x;
  gint                motion_y;
  ClutterModifierType motion_m;

  /* Variables for throttling scroll requests */
  gboolean            scroll_ack;
  gboolean            pending_scroll;
  gint                pending_scroll_x;
  gint                pending_scroll_y;

  /* Variables for synchronous calls */
  ClutterMozEmbedFeedback sync_call;

  /* Locally cached properties */
  gchar           *location;
  gchar           *title;
  gchar           *icon;
  gint             doc_width;
  gint             doc_height;
  gint             scroll_x;
  gint             scroll_y;
  guint            security;
  gboolean         is_loading;
  gdouble          progress;
  gboolean         can_go_back;
  gboolean         can_go_forward;
  GHashTable      *downloads;
  gboolean         scrollbars;
  gboolean         private;

  /* Offsets for async scrolling mode */
  gint             offset_x;
  gint             offset_y;
  gboolean         async_scroll;

  /* Connection timeout variables */
  guint            poll_source;
  guint            poll_timeout;
  guint            poll_timeout_source;
  guint            connect_timeout;
  guint            connect_timeout_source;

  /* List of extra paths to search for components */
  gchar          **comp_paths;
  /* and for chrome manifest files */
  gchar          **chrome_paths;
  /* And for user chrome files */
  gchar           *user_chrome_path;

#ifdef SUPPORT_PLUGINS
  Window           stage_xwin;
  GdkWindow       *stage_gdk_window;
  GtkWidget*       plugin_viewport;
  GtkWidget*       layout_container;
  gboolean         plugin_viewport_initialized;

  GList           *plugin_windows;
#endif

  MozHeadlessCursorType cursor;

#ifdef SUPPORT_IM
    ClutterIMContext *im_context;
    gboolean im_enabled;
#endif
};

ClutterMozEmbedDownload *clutter_mozembed_download_new (ClutterMozEmbed *parent,
                                                        gint             id,
                                                        const gchar     *source,
                                                        const gchar     *dest);
void clutter_mozembed_download_set_progress (ClutterMozEmbedDownload *download,
                                             gint64                   progress,
                                             gint64                   max_progress);
void clutter_mozembed_download_set_complete (ClutterMozEmbedDownload *download,
                                             gboolean                 complete);

void clutter_mozembed_download_set_cancelled (ClutterMozEmbedDownload *download);

#endif /* _CLUTTER_MOZEMBED_PRIVATE */

