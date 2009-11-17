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

#include <gtk/gtk.h>
#include <glib-object.h>
#include <clutter/clutter.h>
#include <clutter/x11/clutter-x11.h>
#include <clutter/glx/clutter-glx.h>
#include <moz-headless.h>
#include <clutter-mozembed/clutter-mozembed-download.h>

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
  ClutterGLXTexturePixmap parent;

  ClutterMozEmbedPrivate *priv;
} ClutterMozEmbed;

typedef struct {
  ClutterGLXTexturePixmapClass parent_class;

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
  void (* show_tooltip) (ClutterMozEmbed *mozembed,
                         const gchar     *text,
                         gint             x,
                         gint             y);
  void (* hide_tooltip) (ClutterMozEmbed *mozembed);
  void (* context_info) (ClutterMozEmbed *mozembed,
                         guint            ctx_type,
                         const gchar     *ctx_uri,
                         const gchar     *ctx_href,
                         const gchar     *ctx_img_href,
                         const gchar     *selected_txt);
} ClutterMozEmbedClass;

/* Security property's flags match Mozilla's nsIWebProgressListener values */
typedef enum {
  CLUTTER_MOZEMBED_IS_BROKEN   = (1 << 0),
  CLUTTER_MOZEMBED_IS_SECURE   = (1 << 1),
  CLUTTER_MOZEMBED_IS_INSECURE = (1 << 2),

  CLUTTER_MOZEMBED_SECURITY_MEDIUM = (1 << 16),
  CLUTTER_MOZEMBED_SECURITY_LOW    = (1 << 17),
  CLUTTER_MOZEMBED_SECURITY_HIGH   = (1 << 18),

  CLUTTER_MOZEMBED_BAD_CERT = (1 << 24)
} ClutterMozEmbedSecurity;

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
void clutter_mozembed_request_close (ClutterMozEmbed *mozembed);
GList *clutter_mozembed_get_downloads (ClutterMozEmbed *mozembed);
void clutter_mozembed_save_uri (ClutterMozEmbed *mozembed,
                                const gchar     *uri,
                                const gchar     *target);
gboolean clutter_mozembed_get_private (ClutterMozEmbed *mozembed);
void clutter_mozembed_purge_session_history (ClutterMozEmbed *mozembed);

gboolean clutter_mozembed_get_scrollbars (ClutterMozEmbed *mozembed);
gboolean clutter_mozembed_get_async_scroll (ClutterMozEmbed *mozembed);
void clutter_mozembed_set_scrollbars (ClutterMozEmbed *mozembed, gboolean show);
void clutter_mozembed_set_async_scroll (ClutterMozEmbed *mozembed,
                                        gboolean         async);
void clutter_mozembed_scroll_by (ClutterMozEmbed *mozembed, gint dx, gint dy);
void clutter_mozembed_scroll_to (ClutterMozEmbed *mozembed, gint x, gint y);

gboolean clutter_mozembed_is_loading (ClutterMozEmbed *mozembed);
gdouble clutter_mozembed_get_progress (ClutterMozEmbed *mozembed);
guint clutter_mozembed_get_security (ClutterMozEmbed *mozembed);

MozHeadlessCursorType clutter_mozembed_get_cursor (ClutterMozEmbed *mozembed);

void clutter_mozembed_lower (ClutterMozEmbed *mozembed);
void clutter_mozembed_raise (ClutterMozEmbed *mozembed);

void clutter_mozembed_set_layout_container (ClutterMozEmbed *mozembed,
                                            GtkWidget       *container);

void clutter_mozembed_set_search_string (ClutterMozEmbed *mozembed,
                                         const gchar     *string);
void clutter_mozembed_find_next (ClutterMozEmbed *mozembed);
void clutter_mozembed_find_prev (ClutterMozEmbed *mozembed);

void clutter_mozembed_set_transparent (ClutterMozEmbed *mozembed,
                                       gboolean         transparent);

G_END_DECLS

#endif /* _CLUTTER_MOZEMBED */

