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
#include <config.h>

#include "clutter-mozembed.h"
#include "clutter-mozembed-comms.h"
#include "clutter-mozembed-private.h"
#include "clutter-mozembed-marshal.h"
#include <moz-headless.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>


G_DEFINE_TYPE (ClutterMozEmbed, clutter_mozembed, CLUTTER_GLX_TYPE_TEXTURE_PIXMAP)

#define MOZEMBED_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), CLUTTER_TYPE_MOZEMBED, ClutterMozEmbedPrivate))

/* #define DEBUG_PLUGIN_VIEWPORT */

enum
{
  PROP_0,

  PROP_LOCATION,
  PROP_TITLE,
  PROP_ICON,
  PROP_READONLY,
  PROP_INPUT,
  PROP_OUTPUT,
  PROP_XID,
  PROP_SPAWN,
  PROP_SCROLLBARS,
  PROP_ASYNC_SCROLL,
  PROP_DOC_WIDTH,
  PROP_DOC_HEIGHT,
  PROP_SCROLL_X,
  PROP_SCROLL_Y,
  PROP_POLL_TIMEOUT,
  PROP_CONNECT_TIMEOUT,
  PROP_CAN_GO_BACK,
  PROP_CAN_GO_FORWARD,
  PROP_CURSOR,
  PROP_SECURITY,
  PROP_COMP_PATHS,
  PROP_CHROME_PATHS,
  PROP_PRIVATE,
  PROP_USER_CHROME_PATH
};

enum
{
  PROGRESS,
  NET_START,
  NET_STOP,
  CRASHED,
  NEW_WINDOW,
  CLOSED,
  LINK_MESSAGE,
  SIZE_REQUEST,
  DOWNLOAD,
  SHOW_TOOLTIP,
  HIDE_TOOLTIP,
  CONTEXT_INFO,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

static void clutter_mozembed_open_pipes (ClutterMozEmbed *self);
static MozHeadlessModifier
  clutter_mozembed_get_modifier (ClutterModifierType modifiers);

#ifdef SUPPORT_PLUGINS
#include "clutter-mozembed-plugin-container.h"

typedef struct _PluginWindow
{
  gint          x, y, width, height;
  ClutterActor *plugin_tfp;
  GtkWidget    *socket;
  guint        plug_id;
} PluginWindow;

static void
clutter_mozembed_allocate_plugins (ClutterMozEmbed        *mozembed,
                                   ClutterAllocationFlags  flags);

static gboolean
clutter_mozembed_init_viewport (ClutterMozEmbed *mozembed);

static void
clutter_mozembed_add_plugin (ClutterMozEmbed *mozembed, guint plug_id,
                             gint x, gint y, gint width, gint height);

static void
clutter_mozembed_update_plugin_bounds (ClutterMozEmbed *mozembed,  guint plug_id,
                                       gint x, gint y, gint width, gint height);

static void
clutter_mozembed_update_plugin_visibility (ClutterMozEmbed *mozembed,  guint plug_id,
                                           gboolean visible);

static int trapped_x_error = 0;
static int (*prev_error_handler) (Display *, XErrorEvent *);
#endif

#ifdef SUPPORT_IM
static void
clutter_mozembed_imcontext_commit_cb (ClutterIMContext *context,
                                      const gchar      *str,
                                      ClutterMozEmbed  *self);
static void
clutter_mozembed_imcontext_preedit_changed_cb (ClutterIMContext *context,
                                               ClutterMozEmbed  *self);
#endif

static void
clamp_offset (ClutterMozEmbed *self)
{
  gint width, height;

  ClutterMozEmbedPrivate *priv = self->priv;

  clutter_texture_get_base_size (CLUTTER_TEXTURE (self), &width, &height);
  priv->offset_x = CLAMP (priv->offset_x,
                          -(priv->doc_width - width - priv->scroll_x),
                          priv->scroll_x);
  priv->offset_y = CLAMP (priv->offset_y,
                          -(priv->doc_height - height - priv->scroll_y),
                          priv->scroll_y);
}

static void
update (ClutterMozEmbed *self,
        Drawable         drawable)
{
  ClutterMozEmbedPrivate *priv = self->priv;

  if (priv->drawable != drawable)
    {
      clutter_x11_texture_pixmap_set_pixmap (CLUTTER_X11_TEXTURE_PIXMAP (self),
                                             (Pixmap)drawable);
      priv->drawable = drawable;
    }
}

static void
send_motion_event (ClutterMozEmbed *self)
{
  ClutterMozEmbedPrivate *priv = self->priv;
  clutter_mozembed_comms_send (priv->output,
                               CME_COMMAND_MOTION,
                               G_TYPE_INT, priv->motion_x,
                               G_TYPE_INT, priv->motion_y,
                               G_TYPE_UINT, clutter_mozembed_get_modifier (
                                              priv->motion_m),
                               G_TYPE_INVALID);
  priv->pending_motion = FALSE;
}

static void
send_scroll_event (ClutterMozEmbed *self)
{
  ClutterMozEmbedPrivate *priv = self->priv;
  clutter_mozembed_comms_send (priv->output,
                               CME_COMMAND_SCROLL_TO,
                               G_TYPE_INT, priv->pending_scroll_x,
                               G_TYPE_INT, priv->pending_scroll_y,
                               G_TYPE_INVALID);
}

static void
_download_finished_cb (ClutterMozEmbedDownload *download,
                       GParamSpec              *pspec,
                       ClutterMozEmbed         *self)
{
  gint id;
  ClutterMozEmbedPrivate *priv = self->priv;

  if (clutter_mozembed_download_get_complete (download) ||
      clutter_mozembed_download_get_cancelled (download))
    {
      g_object_get (G_OBJECT (download), "id", &id, NULL);
      g_hash_table_remove (priv->downloads, GINT_TO_POINTER (id));
    }
}

static gboolean
clutter_mozembed_repaint_func (ClutterMozEmbed *self)
{
  /* Send the paint acknowledgement */
  clutter_mozembed_comms_send (self->priv->output,
                               CME_COMMAND_UPDATE_ACK,
                               G_TYPE_INVALID);
  self->priv->repaint_id = 0;
  return FALSE;
}

static void
process_feedback (ClutterMozEmbed *self, ClutterMozEmbedFeedback feedback)
{
  ClutterMozEmbedPrivate *priv = self->priv;

  /*g_debug ("Processing feedback: %d", feedback);*/

  if (priv->sync_call && (priv->sync_call == feedback))
    priv->sync_call = 0;

  switch (feedback)
    {
    case CME_FEEDBACK_UPDATE :
      {
        Drawable drawable;
        gint doc_width, doc_height, scroll_x, scroll_y;

        clutter_mozembed_comms_receive (priv->input,
                                        G_TYPE_ULONG, &drawable,
                                        G_TYPE_INT, &scroll_x,
                                        G_TYPE_INT, &scroll_y,
                                        G_TYPE_INT, &doc_width,
                                        G_TYPE_INT, &doc_height,
                                        G_TYPE_INVALID);

        if (priv->doc_width != doc_width)
          {
            priv->doc_width = doc_width;
            g_object_notify (G_OBJECT (self), "doc-width");
          }
        if (priv->doc_height != doc_height)
          {
            priv->doc_height = doc_height;
            g_object_notify (G_OBJECT (self), "doc-height");
          }

        /* Update async scrolling offset */
        if (priv->offset_x)
          priv->offset_x += scroll_x - priv->scroll_x;
        if (priv->offset_y)
          priv->offset_y += scroll_y - priv->scroll_y;

        /* Clamp in case document size has changed */
        if (priv->scroll_x != scroll_x)
          {
            priv->scroll_x = scroll_x;
            g_object_notify (G_OBJECT (self), "scroll-x");
          }
        if (priv->scroll_y != scroll_y)
          {
            priv->scroll_y = scroll_y;
            g_object_notify (G_OBJECT (self), "scroll-y");
          }
        clamp_offset (self);

        update (self, drawable);

        priv->repaint_id =
          clutter_threads_add_repaint_func ((GSourceFunc)
                                            clutter_mozembed_repaint_func,
                                            self,
                                            NULL);

        /* We don't queue a redraw, the pixmap update will cause the redraw */

        break;
      }
    case CME_FEEDBACK_MOTION_ACK :
      {
        priv->motion_ack = TRUE;

        if (priv->pending_motion)
          {
            send_motion_event (self);
            priv->motion_ack = FALSE;
            priv->pending_motion = FALSE;
          }
        break;
      }
    case CME_FEEDBACK_SCROLL_ACK :
      {
        priv->scroll_ack = TRUE;

        if (priv->pending_scroll)
          {
            send_scroll_event (self);
            priv->scroll_ack = FALSE;
            priv->pending_scroll = FALSE;
          }

        break;
      }
    case CME_FEEDBACK_PROGRESS :
      {
        priv->progress = clutter_mozembed_comms_receive_double (priv->input);
        g_signal_emit (self, signals[PROGRESS], 0, priv->progress);
        break;
      }
    case CME_FEEDBACK_NET_START :
      {
        priv->is_loading = TRUE;
        priv->progress = 0.0;
        g_signal_emit (self, signals[NET_START], 0);
        break;
      }
    case CME_FEEDBACK_NET_STOP :
      {
        priv->is_loading = FALSE;
        g_signal_emit (self, signals[NET_STOP], 0);
        break;
      }
    case CME_FEEDBACK_LOCATION :
      {
        g_free (priv->location);
        priv->location = clutter_mozembed_comms_receive_string (priv->input);
        g_object_notify (G_OBJECT (self), "location");
        break;
      }
    case CME_FEEDBACK_TITLE :
      {
        g_free (priv->title);
        priv->title = clutter_mozembed_comms_receive_string (priv->input);
        g_object_notify (G_OBJECT (self), "title");
        break;
      }
    case CME_FEEDBACK_ICON :
      {
        g_free (priv->icon);
        priv->icon = clutter_mozembed_comms_receive_string (priv->input);
        g_object_notify (G_OBJECT (self), "icon");
        break;
      }
    case CME_FEEDBACK_CAN_GO_BACK :
      {
        gboolean can_go_back =
          clutter_mozembed_comms_receive_boolean (priv->input);
        if (priv->can_go_back != can_go_back)
          {
            priv->can_go_back = can_go_back;
            g_object_notify (G_OBJECT (self), "can-go-back");
          }
        break;
      }
    case CME_FEEDBACK_CAN_GO_FORWARD :
      {
        gboolean can_go_forward =
          clutter_mozembed_comms_receive_boolean (priv->input);
        if (priv->can_go_forward != can_go_forward)
          {
            priv->can_go_forward = can_go_forward;
            g_object_notify (G_OBJECT (self), "can-go-forward");
          }
        break;
      }
    case CME_FEEDBACK_NEW_WINDOW :
      {
        ClutterMozEmbed *new_window = NULL;
        guint chrome = clutter_mozembed_comms_receive_uint (priv->input);

        /* Find out if the new window is received */
        g_signal_emit (self, signals[NEW_WINDOW], 0, &new_window, chrome);

        /* If it is, send its details to the backend */
        if (new_window)
          {
            gchar *output_file, *input_file;

            output_file = input_file = NULL;
            g_object_get (G_OBJECT (new_window),
                          "output", &output_file,
                          "input", &input_file,
                          NULL);

            clutter_mozembed_comms_send (priv->output,
                                         CME_COMMAND_NEW_WINDOW_RESPONSE,
                                         G_TYPE_BOOLEAN, TRUE,
                                         G_TYPE_STRING, input_file,
                                         G_TYPE_STRING, output_file,
                                         G_TYPE_INVALID);

            g_free (output_file);
            g_free (input_file);
          }
        else
          clutter_mozembed_comms_send (priv->output,
                                       CME_COMMAND_NEW_WINDOW_RESPONSE,
                                       G_TYPE_BOOLEAN, FALSE,
                                       G_TYPE_INVALID);

        break;
      }
    case CME_FEEDBACK_CLOSED :
      {
        /* If we're in dispose, watch_id will be zero */
        if (priv->watch_id)
          g_signal_emit (self, signals[CLOSED], 0);
        break;
      }
    case CME_FEEDBACK_LINK_MESSAGE :
      {
        gchar *link = clutter_mozembed_comms_receive_string (priv->input);
        g_signal_emit (self, signals[LINK_MESSAGE], 0, link);
        g_free (link);
        break;
      }
    case CME_FEEDBACK_SIZE_REQUEST :
      {
        gint width, height;
        clutter_mozembed_comms_receive (priv->input,
                                        G_TYPE_INT, &width,
                                        G_TYPE_INT, &height,
                                        G_TYPE_INVALID);
        g_signal_emit (self, signals[SIZE_REQUEST], 0, width, height);
        break;
      }
    case CME_FEEDBACK_CURSOR :
      {
        priv->cursor = clutter_mozembed_comms_receive_int (priv->input);
        g_object_notify (G_OBJECT (self), "cursor");
        break;
      }
    case CME_FEEDBACK_SECURITY :
      {
        priv->security = clutter_mozembed_comms_receive_int (priv->input);
        g_object_notify (G_OBJECT (self), "security");
        break;
      }
    case CME_FEEDBACK_DL_START :
      {
        gint id;
        gchar *source, *dest;
        ClutterMozEmbedDownload *download;

        clutter_mozembed_comms_receive (priv->input,
                                        G_TYPE_INT, &id,
                                        G_TYPE_STRING, &source,
                                        G_TYPE_STRING, &dest,
                                        G_TYPE_INVALID);

        download = clutter_mozembed_download_new (self, id, source, dest);
        g_hash_table_insert (priv->downloads, GINT_TO_POINTER (id), download);
        g_signal_connect_after (download, "notify::complete",
                                G_CALLBACK (_download_finished_cb), self);
        g_signal_connect_after (download, "notify::cancelled",
                                G_CALLBACK (_download_finished_cb), self);
        g_signal_emit (self, signals[DOWNLOAD], 0, download);

        break;
      }
    case CME_FEEDBACK_DL_PROGRESS :
      {
        gint id;
        gint64 progress, max_progress;
        ClutterMozEmbedDownload *download;

        clutter_mozembed_comms_receive (priv->input,
                                        G_TYPE_INT, &id,
                                        G_TYPE_INT64, &progress,
                                        G_TYPE_INT64, &max_progress,
                                        G_TYPE_INVALID);

        download = g_hash_table_lookup (priv->downloads, GINT_TO_POINTER (id));
        if (download)
          clutter_mozembed_download_set_progress (download,
                                                  progress,
                                                  max_progress);

        break;
      }
    case CME_FEEDBACK_DL_COMPLETE :
      {
        ClutterMozEmbedDownload *download;

        gint id = clutter_mozembed_comms_receive_int (priv->input);

        download = g_hash_table_lookup (priv->downloads, GINT_TO_POINTER (id));
        if (download)
          clutter_mozembed_download_set_complete (download, TRUE);

        break;
      }
    case CME_FEEDBACK_DL_CANCELLED :
      {
        ClutterMozEmbedDownload *download;

        gint id = clutter_mozembed_comms_receive_int (priv->input);

        download = g_hash_table_lookup (priv->downloads, GINT_TO_POINTER (id));
        if (download)
          clutter_mozembed_download_set_cancelled (download);

        break;
      }
    case CME_FEEDBACK_SHOW_TOOLTIP :
      {
        gint x, y;
        gchar *tooltip;

        clutter_mozembed_comms_receive (priv->input,
                                        G_TYPE_INT, &x,
                                        G_TYPE_INT, &y,
                                        G_TYPE_STRING, &tooltip,
                                        G_TYPE_INVALID);

        g_signal_emit (self, signals[SHOW_TOOLTIP], 0, tooltip, x, y);

        break;
      }
    case CME_FEEDBACK_HIDE_TOOLTIP :
      {
        g_signal_emit (self, signals[HIDE_TOOLTIP], 0);
        break;
      }
    case CME_FEEDBACK_PRIVATE :
      {
        gboolean private = clutter_mozembed_comms_receive_boolean (priv->input);

        if (priv->private != private)
          {
            priv->private = private;
            g_object_notify (G_OBJECT (self), "private");
          }

        break;
      }
    case CME_FEEDBACK_PLUGIN_ADDED :
      {
#ifdef SUPPORT_PLUGINS
        guint plug_id;
        gint x, y, width, height;

        clutter_mozembed_comms_receive (priv->input,
                                        G_TYPE_UINT, &plug_id,
                                        G_TYPE_INT, &x,
                                        G_TYPE_INT, &y,
                                        G_TYPE_INT, &width,
                                        G_TYPE_INT, &height,
                                        G_TYPE_INVALID);

        clutter_mozembed_add_plugin (self, plug_id, x, y, width, height);
#endif
        break;
      }
    case CME_FEEDBACK_PLUGIN_UPDATED :
      {
#ifdef SUPPORT_PLUGINS
        guint plug_id;
        gint x, y, width, height;

        clutter_mozembed_comms_receive (priv->input,
                                        G_TYPE_UINT, &plug_id,
                                        G_TYPE_INT, &x,
                                        G_TYPE_INT, &y,
                                        G_TYPE_INT, &width,
                                        G_TYPE_INT, &height,
                                        G_TYPE_INVALID);

        clutter_mozembed_update_plugin_bounds (self, plug_id, x, y, width, height);
#endif
        break;
      }
    case CME_FEEDBACK_PLUGIN_VISIBILITY :
      {
#ifdef SUPPORT_PLUGINS
        guint plug_id;
        gboolean visible;

        clutter_mozembed_comms_receive (priv->input,
                                        G_TYPE_UINT, &plug_id,
                                        G_TYPE_BOOLEAN, &visible,
                                        G_TYPE_INVALID);

        clutter_mozembed_update_plugin_visibility (self, plug_id, visible);
#endif
        break;
      }
#ifdef SUPPORT_IM
    case CME_FEEDBACK_IM_RESET :
      {
        if (!priv->im_enabled)
          break;

        clutter_im_context_reset (priv->im_context);

        break;
      }
    case CME_FEEDBACK_IM_ENABLE :
      {
        priv->im_enabled = clutter_mozembed_comms_receive_boolean (priv->input);

        break;
      }
    case CME_FEEDBACK_IM_FOCUS_CHANGE :
      {
        gboolean in = clutter_mozembed_comms_receive_boolean (priv->input);

        if (!priv->im_enabled)
          break;

        if (in)
          clutter_im_context_focus_in (priv->im_context);
        else
          clutter_im_context_focus_out (priv->im_context);

        break;
      }
    case CME_FEEDBACK_IM_SET_CURSOR :
      {
        ClutterIMRectangle rect;

        clutter_mozembed_comms_receive (priv->input,
                                        G_TYPE_INT, &(rect.x),
                                        G_TYPE_INT, &(rect.y),
                                        G_TYPE_INT, &(rect.width),
                                        G_TYPE_INT, &(rect.height),
                                        G_TYPE_INVALID);

        if (!priv->im_enabled)
          break;

        clutter_im_context_set_cursor_location (priv->im_context, &rect);

        break;
    }
#endif
    case CME_FEEDBACK_CONTEXT_INFO:
      {
        guint type;
        gchar *uri, *href, *img_href, *txt;

        clutter_mozembed_comms_receive (priv->input,
                                        G_TYPE_UINT, &type,
                                        G_TYPE_STRING, &uri,
                                        G_TYPE_STRING, &href,
                                        G_TYPE_STRING, &img_href,
                                        G_TYPE_STRING, &txt,
                                        G_TYPE_INVALID);

        g_signal_emit (self,
                       signals[CONTEXT_INFO],
                       0,
                       type,
                       uri,
                       href,
                       img_href,
                       txt);

        break;
      }
    default :
      g_warning ("Unrecognised feedback received (%d)", feedback);
    }
}

static gboolean
input_io_func (GIOChannel      *source,
               GIOCondition     condition,
               ClutterMozEmbed *self)
{
  /* FYI: Maximum URL length in IE is 2083 characters */
  ClutterMozEmbedFeedback feedback;
  gsize length;
  GError *error = NULL;
  gboolean result = TRUE;

  while (condition & (G_IO_PRI | G_IO_IN))
    {
      GIOStatus status = g_io_channel_read_chars (source,
                                                  (gchar *)(&feedback),
                                                  sizeof (feedback),
                                                  &length, &error);
      if (status == G_IO_STATUS_NORMAL)
        {
          process_feedback (self, feedback);
        }
      else if (status == G_IO_STATUS_ERROR)
        {
          g_warning ("Error reading from source: %s", error->message);
          g_error_free (error);
          result = FALSE;
          break;
        }
      else if (status == G_IO_STATUS_EOF)
        {
          g_warning ("Reached end of input pipe");
          result = FALSE;
          break;
        }

      condition = g_io_channel_get_buffer_condition (source);
    }

  if (condition & G_IO_HUP)
    {
      g_warning ("Unexpected hang-up");
      result = FALSE;
    }

  if (condition & (G_IO_ERR | G_IO_NVAL))
    {
      g_warning ("Error or invalid request");
      result = FALSE;
    }

  if (condition & ~(G_IO_PRI | G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL))
    {
      g_warning ("Unexpected IO condition");
      result = FALSE;
    }

  if (!result)
    g_signal_emit (self, signals[CRASHED], 0);

  return result;
}

void
block_until_feedback (ClutterMozEmbed         *mozembed,
                      ClutterMozEmbedFeedback  feedback)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;

  if (!priv->input)
    return;

  priv->sync_call = feedback;

  /* FIXME: There needs to be a time limit here, or we can hang if the backend
   *        hangs. Here or in input_io_func anyway...
   */
  while (input_io_func (priv->input, G_IO_IN, mozembed) && priv->sync_call);

  if (priv->sync_call)
    g_warning ("Error making synchronous call to backend");
}

#ifdef SUPPORT_PLUGINS
static int
error_handler (Display     *xdpy,
               XErrorEvent *error)
{
  trapped_x_error = error->error_code;
  return 0;
}

void
clutter_mozembed_trap_x_errors (void)
{
  trapped_x_error = 0;
  prev_error_handler = XSetErrorHandler (error_handler);
}

int
clutter_mozembed_untrap_x_errors (void)
{
  XSetErrorHandler (prev_error_handler);
  return trapped_x_error;
}

static PluginWindow *
find_plugin_window_by_socket_xwindow (GList *plugins, Window xwindow)
{
  GList *pwin;
  for (pwin = plugins; pwin; pwin = pwin->next)
    {
      PluginWindow *plugin_window = pwin->data;
      Window        plugin_xwindow =
        GDK_WINDOW_XWINDOW (gtk_widget_get_window (plugin_window->socket));

      if (plugin_xwindow == xwindow)
        return plugin_window;
    }

  return NULL;
}

static PluginWindow *
find_plugin_window_by_plug_xwindow (GList *plugins, Window xwindow)
{
  GList *pwin;
  for (pwin = plugins; pwin; pwin = pwin->next)
    {
      PluginWindow *plugin_window = pwin->data;
      Window        plug_xwindow;
      GdkWindow    *gdk_window;
      gdk_window =
        gtk_socket_get_plug_window (GTK_SOCKET (plugin_window->socket));
      plug_xwindow = GDK_WINDOW_XWINDOW (gdk_window);

      if (plug_xwindow  == xwindow)
        return plugin_window;
    }

  return NULL;
}


static void
clutter_mozembed_sync_plugin_viewport_pos (ClutterMozEmbed *mozembed)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;
  Display                *xdpy = clutter_x11_get_default_display ();
  ClutterGeometry         geom;
  float                   abs_x, abs_y;
  gboolean                mapped;
  gboolean                reactive;
  GdkWindow              *gdk_window;

  if (!priv->plugin_viewport_initialized)
    return;

  mapped = CLUTTER_ACTOR_IS_MAPPED (mozembed);
  reactive = CLUTTER_ACTOR_IS_REACTIVE (mozembed);

  clutter_actor_get_allocation_geometry (CLUTTER_ACTOR (mozembed), &geom);

  clutter_mozembed_trap_x_errors ();

  gdk_window = gtk_widget_get_window (priv->plugin_viewport);

  if (mapped && reactive)
    {
      clutter_actor_get_transformed_position (CLUTTER_ACTOR (mozembed),
                                              &abs_x, &abs_y);

      gdk_window_raise (gdk_window);
      /*
      g_debug("clutter_mozembed_sync_plugin_viewport_pos %d %d %d %d\n",
             (int)abs_x,
             (int)abs_y,
             (int)geom.width,
             (int)geom.height);
      */
      gdk_window_move_resize (gdk_window,
                              (int)abs_x, (int)abs_y,
                              (int)geom.width, (int)geom.height);
    }
  else
    {
      /* Note we don't map/unmap the window since that would conflict with
       * xcomposite and live previews of plugins windows for hidden tabs */
      gdk_window_move (gdk_window, (int)-geom.width, (int)0);
    }

  XSync (xdpy, False);
  clutter_mozembed_untrap_x_errors ();
}

static void
reactive_change_cb (GObject    *object,
                    GParamSpec *param_spec,
                    gpointer    data)
{
  clutter_mozembed_sync_plugin_viewport_pos (CLUTTER_MOZEMBED (object));
}

static void
plugin_socket_mapped (GtkSocket* socket_,
                      GdkEvent*  event,
                      gpointer   user_data)
{
  ClutterMozEmbed        *mozembed = CLUTTER_MOZEMBED (user_data);
  ClutterMozEmbedPrivate *priv = mozembed->priv;
  PluginWindow           *plugin_window;
  GdkWindow              *gdk_win;
  Window                  socket_xwin;
  GdkNativeWindow         plug_window;

  gdk_win = gtk_widget_get_window (GTK_WIDGET (socket_));
  socket_xwin = GDK_WINDOW_XWINDOW (gdk_win);
  plugin_window =
    find_plugin_window_by_socket_xwindow (priv->plugin_windows,
                                          socket_xwin);
  if (!plugin_window || plugin_window->plugin_tfp)
    return;

  plug_window = gtk_socket_get_id (socket_);
  if (!plug_window)
    {
      priv->plugin_windows =
        g_list_remove (priv->plugin_windows, plugin_window);
      g_slice_free (PluginWindow, plugin_window);
      return;
    }

  plugin_window->plugin_tfp =
    clutter_glx_texture_pixmap_new_with_window (socket_xwin);

  clutter_x11_texture_pixmap_set_automatic (
    CLUTTER_X11_TEXTURE_PIXMAP (plugin_window->plugin_tfp),
    TRUE);

  /* We don't want the parent (viewport) window to be automatically
   * updated with changes to the plugin window... */
  g_object_set (G_OBJECT (plugin_window->plugin_tfp),
                "window-redirect-automatic", FALSE,
                NULL);
  clutter_actor_set_parent (plugin_window->plugin_tfp,
                            CLUTTER_ACTOR (mozembed));

  clutter_actor_queue_relayout (CLUTTER_ACTOR (mozembed));
}

static gboolean
plugin_socket_plug_removed (GtkSocket* socket_, gpointer user_data)
{
  ClutterMozEmbed        *mozembed = CLUTTER_MOZEMBED (user_data);
  ClutterMozEmbedPrivate *priv = mozembed->priv;
  PluginWindow           *plugin_window;
  GdkWindow              *gdk_win;
  Window                  socket_xwin;

  gdk_win = gtk_widget_get_window (GTK_WIDGET (socket_));
  socket_xwin = GDK_WINDOW_XWINDOW (gdk_win);

  plugin_window =
    find_plugin_window_by_socket_xwindow (priv->plugin_windows,
                                          socket_xwin);
  if (!plugin_window)
    return FALSE;

  /* g_debug("plugin_socket_plug_removed %p %p\n", plugin_window->plug_id, socket_); */

  priv->plugin_windows = g_list_remove (priv->plugin_windows, plugin_window);
  clutter_actor_unparent (plugin_window->plugin_tfp);
  clutter_actor_queue_relayout (CLUTTER_ACTOR (mozembed));
  g_slice_free (PluginWindow, plugin_window);

  /* by return FALSE, let GTK to destroy the socket */
  return FALSE;
}

static void
plugin_socket_size_allocated (GtkWidget     *socket_,
                              GtkAllocation *allocation,
                              gpointer       user_data)
{
  ClutterMozEmbed        *mozembed = CLUTTER_MOZEMBED (user_data);
  ClutterMozEmbedPrivate *priv = mozembed->priv;
  PluginWindow           *plugin_window;
  GdkWindow              *gdk_win;
  Window                  socket_xwin;

  gdk_win = gtk_widget_get_window (GTK_WIDGET (socket_));
  socket_xwin = GDK_WINDOW_XWINDOW (gdk_win);

  plugin_window =
    find_plugin_window_by_socket_xwindow (priv->plugin_windows,
                                          socket_xwin);

  if (!plugin_window)
    return;

  clutter_mozembed_allocate_plugins (mozembed, FALSE);
  return;
}

static void
clutter_mozembed_add_plugin (ClutterMozEmbed *mozembed,
                             guint            plug_id,
                             gint             x,
                             gint             y,
                             gint             width,
                             gint             height)
{
  PluginWindow* plugin_window;
  ClutterMozEmbedPrivate *priv = mozembed->priv;

  clutter_mozembed_init_viewport (mozembed);
  if (!priv->plugin_viewport_initialized)
    return;

  plugin_window = find_plugin_window_by_plug_xwindow (priv->plugin_windows,
                                                      (Window)plug_id);

  if (!plugin_window)
    {
      /*add a new plugin*/
      plugin_window = g_slice_new (PluginWindow);
      plugin_window->x = x;
      plugin_window->y = y;
      plugin_window->width = width;
      plugin_window->height = height;
      plugin_window->plugin_tfp = NULL;
      plugin_window->plug_id = plug_id;

      plugin_window->socket = gtk_socket_new ();
      /*
      g_debug("clutter_mozembed_add_plugin plug %p, socket %p\n",
        plug_id, plugin_window->socket);
      */
      g_signal_connect (G_OBJECT (plugin_window->socket),
                        "plug-removed",
                        G_CALLBACK (plugin_socket_plug_removed),
                        (gpointer)mozembed);
      g_signal_connect (G_OBJECT (plugin_window->socket),
                        "map-event",
                        G_CALLBACK (plugin_socket_mapped),
                        (gpointer)mozembed);
      g_signal_connect (G_OBJECT (plugin_window->socket),
                        "size-allocate",
                        G_CALLBACK (plugin_socket_size_allocated),
                        (gpointer)mozembed);

      clutter_mozembed_plugin_container_put (
        CLUTTER_MOZEMBED_PLUGIN_CONTAINER (priv->plugin_viewport),
        plugin_window->socket,
        x, y);
      gtk_widget_realize (plugin_window->socket);

      GtkAllocation allocation;
      allocation.x = x;
      allocation.y = y;
      allocation.width = width;
      allocation.height = height;
      gtk_widget_size_allocate (plugin_window->socket, &allocation);

      gtk_socket_add_id (GTK_SOCKET (plugin_window->socket),
                         (GdkNativeWindow)plug_id);

      /* to check if the plug window is removed before
         'plug-removed' handler is instaled */
      if (gtk_socket_get_plug_window (GTK_SOCKET (plugin_window->socket)))
        {
          gtk_widget_show (plugin_window->socket);

          priv->plugin_windows =
            g_list_prepend (priv->plugin_windows, plugin_window);

          clutter_mozembed_sync_plugin_viewport_pos (mozembed);

          g_signal_connect (G_OBJECT (mozembed),
                            "notify::reactive",
                            G_CALLBACK (reactive_change_cb),
                            NULL);
        }
      else
        {
          g_warning ("plug window %u has been destroyed, removing socket %p\n",
                     plug_id,
                     plugin_window->socket);

          gtk_container_remove (GTK_CONTAINER (priv->plugin_viewport),
                                plugin_window->socket);
          g_slice_free (PluginWindow, plugin_window);
        }
    }
}

static void
clutter_mozembed_update_plugin_bounds (ClutterMozEmbed *mozembed,
                                       guint            plug_id,
                                       gint             x,
                                       gint             y,
                                       gint             width,
                                       gint             height)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;
  PluginWindow* plugin_window;

  if (!priv->plugin_viewport_initialized)
    return;

  plugin_window = find_plugin_window_by_plug_xwindow (priv->plugin_windows,
                                                      (Window)plug_id);

  if (plugin_window)
    {
      /*update plugin position and size*/
      /*
        g_debug("clutter_mozembed_update_plugin plug %p %d %d %d %d, socket %p\n",
        plug_id, x, y, width, height, plugin_window->socket);
      */

      plugin_window->x = x;
      plugin_window->y = y;
      plugin_window->width = width;
      plugin_window->height = height;

      clutter_mozembed_plugin_container_move (
        CLUTTER_MOZEMBED_PLUGIN_CONTAINER (priv->plugin_viewport),
        plugin_window->socket,
        x, y,
        width, height);
    }
  else
    {
      g_warning ("clutter_mozembed_update_plugin_bounds plug %u is not added\n",
                 plug_id);
    }
}

static void
clutter_mozembed_update_plugin_visibility (ClutterMozEmbed *mozembed,
                                           guint            plug_id,
                                           gboolean         visible)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;
  PluginWindow* plugin_window = NULL;

  if (!priv->plugin_viewport_initialized)
    return;

  plugin_window = find_plugin_window_by_plug_xwindow (priv->plugin_windows,
                                                      (Window)plug_id);

  if (plugin_window)
    {
      if (visible)
        {
          gdk_window_show (gtk_widget_get_window (plugin_window->socket));
          if (plugin_window->plugin_tfp)
            clutter_actor_show (plugin_window->plugin_tfp);
          clutter_mozembed_sync_plugin_viewport_pos (mozembed);
        }
      else
        {
          gdk_window_hide (gtk_widget_get_window (plugin_window->socket));
          if (plugin_window->plugin_tfp)
            clutter_actor_hide (plugin_window->plugin_tfp);
          clutter_mozembed_sync_plugin_viewport_pos (mozembed);
        }
    }
  else
    {
      g_warning ("clutter_mozembed_update_plugin_visibility plug %u "
                 "is not added\n", plug_id);
    }

}

static void
plugin_viewport_size_allocated (GtkWidget     *viewport,
                                GtkAllocation *allocation,
                                gpointer       user_data)
{
  ClutterMozEmbed        *mozembed = CLUTTER_MOZEMBED (user_data);

  clutter_mozembed_sync_plugin_viewport_pos (CLUTTER_MOZEMBED (mozembed));

  return;
}

static gboolean
clutter_mozembed_init_viewport (ClutterMozEmbed *mozembed)
{
  int composite_major, composite_minor;
  GdkWindow *gdk_window;
  ClutterActor *stage;

  ClutterMozEmbedPrivate *priv = mozembed->priv;
  Display *xdpy = clutter_x11_get_default_display ();

  if(priv->plugin_viewport_initialized == FALSE)
    {
      stage = clutter_actor_get_stage (CLUTTER_ACTOR (mozembed));

      if (!priv->layout_container)
        {
          g_warning ("No plugin container has been set.");
          return FALSE;
        }

      if (!XCompositeQueryVersion (xdpy, &composite_major, &composite_minor))
        {
          g_warning ("The composite extension is required for redirecting "
                     "plugin windows");
          return FALSE;
        }

      priv->plugin_viewport =
        clutter_mozembed_plugin_container_new_for_stage (stage);
      gtk_container_add (GTK_CONTAINER (priv->layout_container),
                         priv->plugin_viewport);

      gdk_window = gtk_widget_get_window (priv->plugin_viewport);
      /* Using XCompositeRedirectWindow instead of gdk_window_set_composited
       * fails on GTK+ >= 2.18
       */
      /*XCompositeRedirectWindow (xdpy,
                                GDK_WINDOW_XWINDOW (gdk_window),
                                CompositeRedirectManual);*/
      gdk_window_set_composited (gdk_window, TRUE);

      gtk_widget_show (priv->plugin_viewport);

      g_signal_connect (G_OBJECT (priv->plugin_viewport),
                        "size-allocate",
                        G_CALLBACK (plugin_viewport_size_allocated),
                        (gpointer)mozembed);

      XSync (xdpy, False);

      priv->plugin_viewport_initialized = TRUE;
    }
  return TRUE;
}

static void
clutter_mozembed_unmap (ClutterActor *actor)
{
  GList *p;
  ClutterMozEmbedPrivate *priv = CLUTTER_MOZEMBED (actor)->priv;

  CLUTTER_ACTOR_CLASS (clutter_mozembed_parent_class)->unmap (actor);
  clutter_mozembed_sync_plugin_viewport_pos (CLUTTER_MOZEMBED (actor));

  for (p = priv->plugin_windows; p; p = p->next)
    {
      PluginWindow *window = p->data;
      if (window->plugin_tfp)
        clutter_actor_unmap (window->plugin_tfp);
    }
}

static void
clutter_mozembed_map (ClutterActor *actor)
{
  GList *p;
  ClutterMozEmbed *mozembed = CLUTTER_MOZEMBED (actor);
  ClutterMozEmbedPrivate *priv = mozembed->priv;

  CLUTTER_ACTOR_CLASS (clutter_mozembed_parent_class)->map (actor);

  for (p = priv->plugin_windows; p; p = p->next)
    {
      PluginWindow *window = p->data;
      if (window->plugin_tfp)
        clutter_actor_map (window->plugin_tfp);
    }

  clutter_mozembed_sync_plugin_viewport_pos (CLUTTER_MOZEMBED (actor));
}
#endif

static void
clutter_mozembed_get_property (GObject *object, guint property_id,
                              GValue *value, GParamSpec *pspec)
{
  ClutterMozEmbed *self = CLUTTER_MOZEMBED (object);

  switch (property_id) {
  case PROP_LOCATION :
    g_value_set_string (value, clutter_mozembed_get_location (self));
    break;

  case PROP_TITLE :
    g_value_set_string (value, clutter_mozembed_get_title (self));
    break;

  case PROP_READONLY :
    g_value_set_boolean (value, self->priv->read_only);
    break;

  case PROP_INPUT :
    g_value_set_string (value, self->priv->input_file);
    break;

  case PROP_OUTPUT :
    g_value_set_string (value, self->priv->output_file);
    break;

  case PROP_XID :
    g_value_set_ulong (value, self->priv->drawable);
    break;

  case PROP_ICON :
    g_value_set_string (value, clutter_mozembed_get_icon (self));
    break;

  case PROP_SCROLLBARS :
    g_value_set_boolean (value, clutter_mozembed_get_scrollbars (self));
    break;

  case PROP_ASYNC_SCROLL :
    g_value_set_boolean (value, clutter_mozembed_get_async_scroll (self));
    break;

  case PROP_DOC_WIDTH :
    g_value_set_int (value, self->priv->doc_width);
    break;

  case PROP_DOC_HEIGHT :
    g_value_set_int (value, self->priv->doc_height);
    break;

  case PROP_SCROLL_X :
    g_value_set_int (value, self->priv->scroll_x);
    break;

  case PROP_SCROLL_Y :
    g_value_set_int (value, self->priv->scroll_y);
    break;

  case PROP_POLL_TIMEOUT :
    g_value_set_uint (value, self->priv->poll_timeout);
    break;

  case PROP_CONNECT_TIMEOUT :
    g_value_set_uint (value, self->priv->connect_timeout);
    break;

  case PROP_CAN_GO_BACK :
    g_value_set_boolean (value, self->priv->can_go_back);
    break;

  case PROP_CAN_GO_FORWARD :
    g_value_set_boolean (value, self->priv->can_go_forward);
    break;

  case PROP_CURSOR :
    g_value_set_uint (value, self->priv->cursor);
    break;

  case PROP_SECURITY :
    g_value_set_uint (value, self->priv->security);
    break;

  case PROP_COMP_PATHS :
    g_value_set_boxed (value, self->priv->comp_paths);
    break;

  case PROP_CHROME_PATHS :
    g_value_set_boxed (value, self->priv->chrome_paths);
    break;

  case PROP_PRIVATE :
    g_value_set_boolean (value, self->priv->private);
    break;

  case PROP_USER_CHROME_PATH :
    g_value_set_string (value, self->priv->user_chrome_path);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
clutter_mozembed_set_property (GObject *object, guint property_id,
                              const GValue *value, GParamSpec *pspec)
{
  ClutterMozEmbed *self = CLUTTER_MOZEMBED (object);
  ClutterMozEmbedPrivate *priv = self->priv;

  switch (property_id) {
  case PROP_READONLY :
    priv->read_only = g_value_get_boolean (value);
    break;

  case PROP_INPUT :
    priv->input_file = g_value_dup_string (value);
    break;

  case PROP_OUTPUT :
    priv->output_file = g_value_dup_string (value);
    break;

  case PROP_SPAWN :
    priv->spawn = g_value_get_boolean (value);
    break;

  case PROP_SCROLLBARS :
    clutter_mozembed_set_scrollbars (self, g_value_get_boolean (value));
    break;

  case PROP_ASYNC_SCROLL :
    clutter_mozembed_set_async_scroll (self, g_value_get_boolean (value));
    break;

  case PROP_SCROLL_X :
    clutter_mozembed_scroll_to (self, g_value_get_int (value), priv->scroll_y);
    break;

  case PROP_SCROLL_Y :
    clutter_mozembed_scroll_to (self, priv->scroll_x, g_value_get_int (value));
    break;

  case PROP_POLL_TIMEOUT :
    priv->poll_timeout = g_value_get_uint (value);
    break;

  case PROP_CONNECT_TIMEOUT :
    priv->connect_timeout = g_value_get_uint (value);
    break;

  case PROP_CURSOR :
    priv->cursor = g_value_get_uint (value);
    break;

  case PROP_COMP_PATHS :
    priv->comp_paths = g_strdupv (g_value_get_boxed (value));
    break;

  case PROP_CHROME_PATHS :
    priv->chrome_paths = g_strdupv (g_value_get_boxed (value));
    break;

  case PROP_PRIVATE :
    priv->private = g_value_get_boolean (value);
    break;

  case PROP_USER_CHROME_PATH :
    g_free (priv->user_chrome_path);
    priv->user_chrome_path = g_value_dup_string (value);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
disconnect_poll_sources (ClutterMozEmbed *self)
{
  ClutterMozEmbedPrivate *priv = self->priv;

  if (priv->poll_source)
    {
      g_source_remove (priv->poll_source);
      priv->poll_source = 0;
    }

  if (priv->poll_timeout_source)
    {
      g_source_remove (priv->poll_timeout_source);
      priv->poll_timeout_source = 0;
    }
}

gboolean
idle_monitor_unref_cb (gpointer data)
{
  g_object_unref (data);
  return FALSE;
}

static void
disconnect_file_monitor_sources (ClutterMozEmbed *self)
{
  ClutterMozEmbedPrivate *priv = self->priv;

  if (priv->monitor)
    {
      g_file_monitor_cancel (priv->monitor);
#ifndef DISABLE_GFILEMONITOR_BUG_WORKAROUND
      /* gio/gilemonitor.c:emit_cb does not cope if we dispose a monitor when
       * there are multiple pending_file_changes. */
      g_idle_add (idle_monitor_unref_cb, priv->monitor);
#else
      g_object_unref (priv->monitor);
#endif
      priv->monitor = NULL;
    }

  if (priv->watch_id)
    {
      g_source_remove (priv->watch_id);
      priv->watch_id = 0;
    }
}

static void
clutter_mozembed_dispose (GObject *object)
{
  ClutterMozEmbed *self = CLUTTER_MOZEMBED (object);
  ClutterMozEmbedPrivate *priv = self->priv;

#ifdef SUPPORT_PLUGINS
  while (priv->plugin_windows)
    {
      PluginWindow *plugin_window = priv->plugin_windows->data;
      if (plugin_window->plugin_tfp)
        clutter_actor_unparent (plugin_window->plugin_tfp);
      g_slice_free (PluginWindow, plugin_window);
      priv->plugin_windows = g_list_delete_link (priv->plugin_windows,
                                                 priv->plugin_windows);
    }

  if (priv->plugin_viewport_initialized)
    {
      gtk_widget_hide (priv->plugin_viewport);
      gtk_widget_destroy (priv->plugin_viewport);

      priv->plugin_viewport = NULL;
      priv->plugin_viewport_initialized = FALSE;
    }
#endif

  disconnect_poll_sources (self);
  disconnect_file_monitor_sources (self);

  if (priv->connect_timeout_source)
    {
      g_source_remove (priv->connect_timeout_source);
      priv->connect_timeout_source = 0;
    }

  if (priv->input)
    {
      GError *error = NULL;

      if (g_io_channel_shutdown (priv->input, FALSE, &error) ==
          G_IO_STATUS_ERROR)
        {
          g_warning ("Error closing IO channel: %s", error->message);
          g_error_free (error);
        }

      g_io_channel_unref (priv->input);
      priv->input = NULL;
    }

  if (priv->output)
    {
      GError *error = NULL;

      if (g_io_channel_shutdown (priv->output, FALSE, &error) ==
          G_IO_STATUS_ERROR)
        {
          g_warning ("Error closing IO channel: %s", error->message);
          g_error_free (error);
        }

      g_io_channel_unref (priv->output);
      priv->output = NULL;
    }

  if (priv->downloads)
    {
      g_hash_table_unref (priv->downloads);
      priv->downloads = NULL;
    }

  if (priv->repaint_id)
    {
      clutter_threads_remove_repaint_func (priv->repaint_id);
      priv->repaint_id = 0;
    }

  G_OBJECT_CLASS (clutter_mozembed_parent_class)->dispose (object);
}

static void
clutter_mozembed_finalize (GObject *object)
{
  ClutterMozEmbedPrivate *priv = CLUTTER_MOZEMBED (object)->priv;

  g_remove (priv->output_file);
  g_remove (priv->input_file);

  g_free (priv->location);
  g_free (priv->title);
  g_free (priv->input_file);
  g_free (priv->output_file);

  g_strfreev (priv->comp_paths);
  g_strfreev (priv->chrome_paths);
  g_free (priv->user_chrome_path);

  G_OBJECT_CLASS (clutter_mozembed_parent_class)->finalize (object);
}

#ifdef SUPPORT_PLUGINS
static void
clutter_mozembed_allocate_plugins (ClutterMozEmbed        *mozembed,
                                   ClutterAllocationFlags  flags)
{
  GList *pwin;

  ClutterMozEmbedPrivate *priv = mozembed->priv;

  for (pwin = priv->plugin_windows; pwin != NULL; pwin = pwin->next)
    {
      PluginWindow   *plugin_window = pwin->data;
      ClutterActor   *plugin_tfp = CLUTTER_ACTOR (plugin_window->plugin_tfp);
      gfloat          natural_width, natural_height;
      ClutterActorBox child_box;

      /* Note, the texture-from-pixmap actor is considered authorative over
       * its width and height as opposed to us tracking the width and height
       * of the actor according to configure notify events of the corresponding
       * plugin window.
       *
       * If the tfp actor hasn't yet renamed the pixmap for the redirected
       * window since it was last resized, then I guess it will look better if
       * we avoid scaling whatever pixmap we currently have. */
      if(plugin_tfp)
        {
          clutter_actor_get_preferred_size (plugin_tfp,
                                            NULL, NULL,
                                            &natural_width, &natural_height);

          child_box.x1 = plugin_window->x;
          child_box.y1 = plugin_window->y;
          child_box.x2 = plugin_window->x + natural_width;
          child_box.y2 = plugin_window->y + natural_height;

          clutter_actor_allocate (plugin_window->plugin_tfp, &child_box, flags);
        }
    }
}
#endif

static void
clutter_mozembed_get_preferred_width (ClutterActor *actor,
                                      gfloat        for_height,
                                      gfloat       *min_width_p,
                                      gfloat       *natural_width_p)
{
  ClutterMozEmbed *mozembed = CLUTTER_MOZEMBED (actor);
  ClutterMozEmbedPrivate *priv = mozembed->priv;

  if (priv->read_only)
    {
      gint width, height;

      if (min_width_p)
        *min_width_p = 0;

      clutter_texture_get_base_size (CLUTTER_TEXTURE (actor),
                                     &width, &height);

      if (natural_width_p)
        {
          if (for_height > 0)
            *natural_width_p = width / (gfloat)height * for_height;
          else
            *natural_width_p = (gfloat)width;
        }
    }
  else
    CLUTTER_ACTOR_CLASS (clutter_mozembed_parent_class)->
      get_preferred_width (actor, for_height, min_width_p, natural_width_p);
}

static void
clutter_mozembed_get_preferred_height (ClutterActor *actor,
                                       gfloat        for_width,
                                       gfloat       *min_height_p,
                                       gfloat       *natural_height_p)
{
  ClutterMozEmbed *mozembed = CLUTTER_MOZEMBED (actor);
  ClutterMozEmbedPrivate *priv = mozembed->priv;

  if (priv->read_only)
    {
      gint width, height;

      if (min_height_p)
        *min_height_p = 0;

      clutter_texture_get_base_size (CLUTTER_TEXTURE (actor),
                                     &width, &height);

      if (natural_height_p)
        {
          if (for_width > 0)
            *natural_height_p = height / (gfloat)width * for_width;
          else
            *natural_height_p = (gfloat)height;
        }
    }
  else
    CLUTTER_ACTOR_CLASS (clutter_mozembed_parent_class)->
      get_preferred_height (actor, for_width, min_height_p, natural_height_p);
}

static void
clutter_mozembed_allocate (ClutterActor           *actor,
                           const ClutterActorBox  *box,
                           ClutterAllocationFlags  flags)
{
  gint width, height, tex_width, tex_height;
  ClutterMozEmbed *mozembed = CLUTTER_MOZEMBED (actor);
  ClutterMozEmbedPrivate *priv = mozembed->priv;

  width = (gint)(box->x2 - box->x1);
  height = (gint)(box->y2 - box->y1);

  if (width < 0 || height < 0)
    return;

  clutter_texture_get_base_size (CLUTTER_TEXTURE (actor),
                                 &tex_width, &tex_height);

  if ((!priv->read_only) &&
      (((tex_width != width) || (tex_height != height)) ||
       !priv->drawable))
    {
      clutter_x11_texture_pixmap_set_pixmap (CLUTTER_X11_TEXTURE_PIXMAP (actor),
                                             None);
      priv->drawable = None;
      priv->width = width;
      priv->height = height;

      /* Send a resize command to the back-end */
      clutter_mozembed_comms_send (priv->output,
                                   CME_COMMAND_RESIZE,
                                   G_TYPE_INT, width,
                                   G_TYPE_INT, height,
                                   G_TYPE_INVALID);
    }

  CLUTTER_ACTOR_CLASS (clutter_mozembed_parent_class)->
    allocate (actor, box, flags);

#ifdef SUPPORT_PLUGINS
  if (priv->plugin_viewport)
    {
      clutter_mozembed_sync_plugin_viewport_pos (mozembed);
      clutter_mozembed_allocate_plugins (mozembed, flags);
    }
#endif
}

static void
clutter_mozembed_paint (ClutterActor *actor)
{
  ClutterMozEmbed *self = CLUTTER_MOZEMBED (actor);
  ClutterMozEmbedPrivate *priv = self->priv;
  ClutterGeometry geom;
#ifdef SUPPORT_PLUGINS
  GList *pwin;
#endif

  clutter_actor_get_allocation_geometry (actor, &geom);

  /* Offset if we're using async scrolling */
  if (priv->async_scroll || !priv->read_only)
    {
      cogl_clip_push (0, 0, geom.width, geom.height);
      if (priv->async_scroll)
        cogl_translate (priv->offset_x, priv->offset_y, 0);
    }

  /* Paint texture */
  if (priv->read_only)
    CLUTTER_ACTOR_CLASS (clutter_mozembed_parent_class)->paint (actor);
  else
    {
      CoglHandle material =
        clutter_texture_get_cogl_material (CLUTTER_TEXTURE (actor));
      if (material != COGL_INVALID_HANDLE)
        {
          guint opacity;
          gint width, height;

          clutter_texture_get_base_size (CLUTTER_TEXTURE (actor),
                                         &width, &height);
          opacity = clutter_actor_get_paint_opacity (actor);

          cogl_material_set_color4ub (material,
                                      opacity, opacity, opacity, opacity);
          cogl_set_source (material);
          cogl_rectangle (0, 0, width, height);
        }
    }

#ifdef SUPPORT_PLUGINS
  /* Paint plugin windows */
  cogl_clip_push (0, 0, geom.width, geom.height);
  for (pwin = priv->plugin_windows; pwin != NULL; pwin = pwin->next)
    {
      PluginWindow *plugin_window = pwin->data;
      ClutterActor *plugin_tfp = CLUTTER_ACTOR (plugin_window->plugin_tfp);

      if (plugin_tfp && CLUTTER_ACTOR_IS_MAPPED (plugin_tfp))
        clutter_actor_paint (plugin_tfp);
    }
  cogl_clip_pop ();
#endif

  if (priv->async_scroll || !priv->read_only)
    cogl_clip_pop ();
}

static void
clutter_mozembed_pick (ClutterActor *actor, const ClutterColor *c)
{
  float width, height;

  cogl_set_source_color4ub (c->red, c->green, c->blue, c->alpha);
  clutter_actor_get_size (actor, &width, &height);
  cogl_rectangle (0, 0, width, height);
}

static MozHeadlessModifier
clutter_mozembed_get_modifier (ClutterModifierType modifiers)
{
  MozHeadlessModifier mozifiers = 0;
  mozifiers |= (modifiers & CLUTTER_SHIFT_MASK) ? MOZ_KEY_SHIFT_MASK : 0;
  mozifiers |= (modifiers & CLUTTER_CONTROL_MASK) ? MOZ_KEY_CONTROL_MASK : 0;
  mozifiers |= (modifiers & CLUTTER_MOD1_MASK) ? MOZ_KEY_ALT_MASK : 0;
  mozifiers |= (modifiers & CLUTTER_META_MASK) ? MOZ_KEY_META_MASK : 0;
  mozifiers |= (modifiers & CLUTTER_LOCK_MASK) ? MOZ_KEY_LOCK_MASK : 0;

  return mozifiers;
}

static void
clutter_mozembed_key_focus_in (ClutterActor *actor)
{
  ClutterMozEmbedPrivate *priv = CLUTTER_MOZEMBED (actor)->priv;
  clutter_mozembed_comms_send (priv->output,
                               CME_COMMAND_FOCUS,
                               G_TYPE_BOOLEAN, TRUE,
                               G_TYPE_INVALID);
}

static void
clutter_mozembed_key_focus_out (ClutterActor *actor)
{
  ClutterMozEmbedPrivate *priv = CLUTTER_MOZEMBED (actor)->priv;
  clutter_mozembed_comms_send (priv->output,
                               CME_COMMAND_FOCUS,
                               G_TYPE_BOOLEAN, FALSE,
                               G_TYPE_INVALID);
}

static gboolean
clutter_mozembed_motion_event (ClutterActor *actor, ClutterMotionEvent *event)
{
  ClutterMozEmbedPrivate *priv;
  gfloat x_out, y_out;

  priv = CLUTTER_MOZEMBED (actor)->priv;

  if (!clutter_actor_transform_stage_point (actor,
                                            (gfloat)event->x,
                                            (gfloat)event->y,
                                            &x_out, &y_out))
    return FALSE;

  priv->motion_x = (gint)x_out;
  priv->motion_y = (gint)y_out;
  priv->motion_m = event->modifier_state;

  /* Throttle motion events while there's new data waiting, otherwise we can
   * peg the back-end rendering new frames. (back-end sends 'mack' when it
   * finishes processing a motion event)
   */
  if (!priv->motion_ack)
    {
      priv->pending_motion = TRUE;
      return TRUE;
    }

  send_motion_event (CLUTTER_MOZEMBED (actor));

  priv->motion_ack = FALSE;

  return TRUE;
}

static gboolean
clutter_mozembed_button_press_event (ClutterActor *actor,
                                     ClutterButtonEvent *event)
{
  ClutterMozEmbedPrivate *priv;
  gfloat x_out, y_out;

  priv = CLUTTER_MOZEMBED (actor)->priv;

  if (!clutter_actor_transform_stage_point (actor,
                                            (gfloat)event->x,
                                            (gfloat)event->y,
                                            &x_out, &y_out))
    return FALSE;

  clutter_grab_pointer (actor);

  clutter_mozembed_comms_send (priv->output,
                               CME_COMMAND_BUTTON_PRESS,
                               G_TYPE_INT, (gint)x_out,
                               G_TYPE_INT, (gint)y_out,
                               G_TYPE_INT, event->button,
                               G_TYPE_INT, event->click_count,
                               G_TYPE_UINT, clutter_mozembed_get_modifier (
                                              event->modifier_state),
                               G_TYPE_INVALID);

  return TRUE;
}

static gboolean
clutter_mozembed_button_release_event (ClutterActor *actor,
                                       ClutterButtonEvent *event)
{
  ClutterMozEmbedPrivate *priv;
  gfloat x_out, y_out;

  clutter_ungrab_pointer ();

  priv = CLUTTER_MOZEMBED (actor)->priv;

  if (!clutter_actor_transform_stage_point (actor,
                                            (gfloat)event->x,
                                            (gfloat)event->y,
                                            &x_out, &y_out))
    return FALSE;

  clutter_mozembed_comms_send (priv->output,
                               CME_COMMAND_BUTTON_RELEASE,
                               G_TYPE_INT, (gint)x_out,
                               G_TYPE_INT, (gint)y_out,
                               G_TYPE_INT, event->button,
                               G_TYPE_UINT, clutter_mozembed_get_modifier (
                                              event->modifier_state),
                               G_TYPE_INVALID);

  return TRUE;
}

struct mozKeyConverter {
    int vkCode; /* Platform independent key code */
    int keysym; /* CLUTTER keysym key code */
};

struct mozKeyConverter mozKeycodes[] = {
    { MOZ_KEY_CANCEL,     CLUTTER_Cancel },
    { MOZ_KEY_BACK_SPACE, CLUTTER_BackSpace },
    { MOZ_KEY_TAB,        CLUTTER_Tab },
    { MOZ_KEY_TAB,        CLUTTER_ISO_Left_Tab },
    { MOZ_KEY_CLEAR,      CLUTTER_Clear },
    { MOZ_KEY_RETURN,     CLUTTER_Return },
    { MOZ_KEY_SHIFT,      CLUTTER_Shift_L },
    { MOZ_KEY_SHIFT,      CLUTTER_Shift_R },
    { MOZ_KEY_CONTROL,    CLUTTER_Control_L },
    { MOZ_KEY_CONTROL,    CLUTTER_Control_R },
    { MOZ_KEY_ALT,        CLUTTER_Alt_L },
    { MOZ_KEY_ALT,        CLUTTER_Alt_R },
    { MOZ_KEY_META,       CLUTTER_Meta_L },
    { MOZ_KEY_META,       CLUTTER_Meta_R },
    { MOZ_KEY_PAUSE,      CLUTTER_Pause },
    { MOZ_KEY_CAPS_LOCK,  CLUTTER_Caps_Lock },
    { MOZ_KEY_ESCAPE,     CLUTTER_Escape },
    { MOZ_KEY_SPACE,      CLUTTER_space },
    { MOZ_KEY_PAGE_UP,    CLUTTER_Page_Up },
    { MOZ_KEY_PAGE_DOWN,  CLUTTER_Page_Down },
    { MOZ_KEY_END,        CLUTTER_End },
    { MOZ_KEY_HOME,       CLUTTER_Home },
    { MOZ_KEY_LEFT,       CLUTTER_Left },
    { MOZ_KEY_UP,         CLUTTER_Up },
    { MOZ_KEY_RIGHT,      CLUTTER_Right },
    { MOZ_KEY_DOWN,       CLUTTER_Down },
    { MOZ_KEY_PRINTSCREEN, CLUTTER_Print },
    { MOZ_KEY_INSERT,     CLUTTER_Insert },
    { MOZ_KEY_DELETE,     CLUTTER_Delete },

    /* keypad keys */
    { MOZ_KEY_LEFT,       CLUTTER_KP_Left },
    { MOZ_KEY_RIGHT,      CLUTTER_KP_Right },
    { MOZ_KEY_UP,         CLUTTER_KP_Up },
    { MOZ_KEY_DOWN,       CLUTTER_KP_Down },
    { MOZ_KEY_PAGE_UP,    CLUTTER_KP_Page_Up },
    /* Not sure what these are */
    /*{ MOZ_KEY_,       CLUTTER_KP_Prior },
    { MOZ_KEY_,        CLUTTER_KP_Next },
     CLUTTER_KP_Begin is the 5 on the non-numlock keypad
    { MOZ_KEY_,        CLUTTER_KP_Begin },*/
    { MOZ_KEY_PAGE_DOWN,  CLUTTER_KP_Page_Down },
    { MOZ_KEY_HOME,       CLUTTER_KP_Home },
    { MOZ_KEY_END,        CLUTTER_KP_End },
    { MOZ_KEY_INSERT,     CLUTTER_KP_Insert },
    { MOZ_KEY_DELETE,     CLUTTER_KP_Delete },

    { MOZ_KEY_MULTIPLY,   CLUTTER_KP_Multiply },
    { MOZ_KEY_ADD,        CLUTTER_KP_Add },
    { MOZ_KEY_SEPARATOR,  CLUTTER_KP_Separator },
    { MOZ_KEY_SUBTRACT,   CLUTTER_KP_Subtract },
    { MOZ_KEY_DECIMAL,    CLUTTER_KP_Decimal },
    { MOZ_KEY_DIVIDE,     CLUTTER_KP_Divide },
    { MOZ_KEY_RETURN,     CLUTTER_KP_Enter },
    { MOZ_KEY_NUM_LOCK,   CLUTTER_Num_Lock },
    { MOZ_KEY_SCROLL_LOCK,CLUTTER_Scroll_Lock },

    { MOZ_KEY_COMMA,      CLUTTER_comma },
    { MOZ_KEY_PERIOD,     CLUTTER_period },
    { MOZ_KEY_SLASH,      CLUTTER_slash },
    { MOZ_KEY_BACK_SLASH, CLUTTER_backslash },
    { MOZ_KEY_BACK_QUOTE, CLUTTER_grave },
    { MOZ_KEY_OPEN_BRACKET, CLUTTER_bracketleft },
    { MOZ_KEY_CLOSE_BRACKET, CLUTTER_bracketright },
    { MOZ_KEY_SEMICOLON, CLUTTER_colon },
    { MOZ_KEY_QUOTE, CLUTTER_apostrophe },

    /* context menu key, keysym 0xff67, typically keycode 117 on 105-key
     * (Microsoft) x86 keyboards, located between right 'Windows' key and
     * right Ctrl key */
    { MOZ_KEY_CONTEXT_MENU, CLUTTER_Menu },

    /* NS doesn't have dash or equals distinct from the numeric keypad ones,
     * so we'll use those for now.  See bug 17008: */
    { MOZ_KEY_SUBTRACT, CLUTTER_minus },
    { MOZ_KEY_EQUALS, CLUTTER_equal },

    /* Some shifted keys, see bug 15463 as well as 17008.
     * These should be subject to different keyboard mappings. */
    { MOZ_KEY_QUOTE, CLUTTER_quotedbl },
    { MOZ_KEY_OPEN_BRACKET, CLUTTER_braceleft },
    { MOZ_KEY_CLOSE_BRACKET, CLUTTER_braceright },
    { MOZ_KEY_BACK_SLASH, CLUTTER_bar },
    { MOZ_KEY_SEMICOLON, CLUTTER_semicolon },
    { MOZ_KEY_BACK_QUOTE, CLUTTER_asciitilde },
    { MOZ_KEY_COMMA, CLUTTER_less },
    { MOZ_KEY_PERIOD, CLUTTER_greater },
    { MOZ_KEY_SLASH,      CLUTTER_question },
    { MOZ_KEY_1, CLUTTER_exclam },
    { MOZ_KEY_2, CLUTTER_at },
    { MOZ_KEY_3, CLUTTER_numbersign },
    { MOZ_KEY_4, CLUTTER_dollar },
    { MOZ_KEY_5, CLUTTER_percent },
    { MOZ_KEY_6, CLUTTER_asciicircum },
    { MOZ_KEY_7, CLUTTER_ampersand },
    { MOZ_KEY_8, CLUTTER_asterisk },
    { MOZ_KEY_9, CLUTTER_parenleft },
    { MOZ_KEY_0, CLUTTER_parenright },
    { MOZ_KEY_SUBTRACT, CLUTTER_underscore },
    { MOZ_KEY_EQUALS, CLUTTER_plus }
};

static gboolean
clutter_mozembed_get_keyval (ClutterKeyEvent *event, guint *keyval)
{
  int i, length = 0;

  /* letters*/
  if (event->keyval >= CLUTTER_a && event->keyval <= CLUTTER_z)
    {
      *keyval = event->keyval - CLUTTER_a + MOZ_KEY_A;
      return TRUE;
    }
  if (event->keyval >= CLUTTER_A && event->keyval <= CLUTTER_Z)
    {
      *keyval = event->keyval - CLUTTER_A + MOZ_KEY_A;
      return TRUE;
    }

  /* numbers */
  if ( event->keyval >= CLUTTER_0 && event->keyval <= CLUTTER_9)
    {
      *keyval = event->keyval - CLUTTER_0 + MOZ_KEY_0;
      return TRUE;
    }

  /* keypad numbers */
  if (event->keyval >= CLUTTER_KP_0 && event->keyval <= CLUTTER_KP_9)
    {
      *keyval = event->keyval - CLUTTER_KP_0 + MOZ_KEY_NUMPAD0;
      return TRUE;
    }

  /* misc other things */
  length = sizeof (mozKeycodes) / sizeof (struct mozKeyConverter);
  for (i = 0; i < length; i++) {
    if (mozKeycodes[i].keysym == event->keyval)
      {
        *keyval = (mozKeycodes[i].vkCode);
        return TRUE;
      }
  }

  /* function keys */
  if (event->keyval >= CLUTTER_F1 && event->keyval <= CLUTTER_F24)
    {
      *keyval = event->keyval - CLUTTER_F1 + MOZ_KEY_F1;
      return TRUE;
    }

  /* Any ascii we might've missed */
  if (g_ascii_isalnum (event->unicode_value))
    {
      *keyval = event->unicode_value;
      return TRUE;
    }

  /* Unhandled */
  *keyval = 0;

  return FALSE;
}

static gboolean
clutter_mozembed_key_press_event (ClutterActor *actor, ClutterKeyEvent *event)
{
  ClutterMozEmbedPrivate *priv;
  guint keyval;

  priv = CLUTTER_MOZEMBED (actor)->priv;

#ifdef SUPPORT_IM
  if (priv->im_enabled &&
      clutter_im_context_filter_keypress (priv->im_context, event))
    return TRUE;
#endif

  if ((!clutter_mozembed_get_keyval (event, &keyval)) &&
      (event->unicode_value == '\0'))
    return FALSE;

  clutter_mozembed_comms_send (priv->output,
                               CME_COMMAND_KEY_PRESS,
                               G_TYPE_UINT, keyval,
                               G_TYPE_UINT, event->unicode_value,
                               G_TYPE_UINT, clutter_mozembed_get_modifier (
                                              event->modifier_state),
                               G_TYPE_INVALID);

  return TRUE;
}

static gboolean
clutter_mozembed_key_release_event (ClutterActor *actor, ClutterKeyEvent *event)
{
  ClutterMozEmbedPrivate *priv;
  guint keyval;

  priv = CLUTTER_MOZEMBED (actor)->priv;

#ifdef SUPPORT_IM
  if (priv->im_enabled &&
      clutter_im_context_filter_keypress (priv->im_context, event))
    return TRUE;
#endif

  if (clutter_mozembed_get_keyval (event, &keyval))
    {
      clutter_mozembed_comms_send (priv->output,
                                   CME_COMMAND_KEY_RELEASE,
                                   G_TYPE_UINT, keyval,
                                   G_TYPE_UINT, clutter_mozembed_get_modifier (
                                                  event->modifier_state),
                                   G_TYPE_INVALID);
      return TRUE;
    }

  return FALSE;
}

static gboolean
clutter_mozembed_scroll_event (ClutterActor *actor,
                               ClutterScrollEvent *event)
{
  ClutterMozEmbedPrivate *priv;
  gfloat x_out, y_out;
  gint button;

  priv = CLUTTER_MOZEMBED (actor)->priv;

  if (!clutter_actor_transform_stage_point (actor,
                                            (gfloat)event->x,
                                            (gfloat)event->y,
                                            &x_out, &y_out))
    return FALSE;

  switch (event->direction)
    {
    case CLUTTER_SCROLL_UP :
      button = 4;
      break;
    default:
    case CLUTTER_SCROLL_DOWN :
      button = 5;
      break;
    case CLUTTER_SCROLL_LEFT :
      button = 6;
      break;
    case CLUTTER_SCROLL_RIGHT :
      button = 7;
      break;
    }

  if (!priv->scrollbars)
    {
      /* FIXME: Maybe we shouldn't do this? */
      switch (event->direction)
        {
        case CLUTTER_SCROLL_UP :
          clutter_mozembed_scroll_by (CLUTTER_MOZEMBED (actor), 0, -50);
          break;
        default:
        case CLUTTER_SCROLL_DOWN :
          clutter_mozembed_scroll_by (CLUTTER_MOZEMBED (actor), 0, 50);
          break;
        case CLUTTER_SCROLL_LEFT :
          clutter_mozembed_scroll_by (CLUTTER_MOZEMBED (actor), -50, 0);
          break;
        case CLUTTER_SCROLL_RIGHT :
          clutter_mozembed_scroll_by (CLUTTER_MOZEMBED (actor), 50, 0);
          break;
        }
    }

  clutter_mozembed_comms_send (priv->output,
                               CME_COMMAND_BUTTON_PRESS,
                               G_TYPE_INT, (gint)x_out,
                               G_TYPE_INT, (gint)y_out,
                               G_TYPE_INT, button,
                               G_TYPE_INT, 1,
                               G_TYPE_UINT, clutter_mozembed_get_modifier (
                                              event->modifier_state),
                               G_TYPE_INVALID);

  return TRUE;
}

static void
file_changed_cb (GFileMonitor      *monitor,
                 GFile             *file,
                 GFile             *other_file,
                 GFileMonitorEvent  event_type,
                 ClutterMozEmbed   *self)
{
  gint fd;
  ClutterMozEmbedPrivate *priv = self->priv;

  if (event_type != G_FILE_MONITOR_EVENT_CREATED)
    return;

  disconnect_poll_sources (self);
  disconnect_file_monitor_sources (self);

  if (priv->connect_timeout_source)
    {
      g_source_remove (priv->connect_timeout_source);
      priv->connect_timeout_source = 0;
    }

  /* Open input channel */
  fd = open (priv->output_file, O_RDONLY | O_NONBLOCK);
  priv->input = g_io_channel_unix_new (fd);
  g_io_channel_set_encoding (priv->input, NULL, NULL);
  g_io_channel_set_buffered (priv->input, FALSE);
  g_io_channel_set_close_on_unref (priv->input, TRUE);
  priv->watch_id = g_io_add_watch (priv->input,
                                   G_IO_IN | G_IO_PRI | G_IO_ERR |
                                   G_IO_NVAL | G_IO_HUP,
                                   (GIOFunc)input_io_func,
                                   self);

  priv->is_loading = FALSE;
}

static gboolean
poll_idle_cb (ClutterMozEmbed *self)
{
  ClutterMozEmbedPrivate *priv = self->priv;

  if (g_file_test (priv->output_file, G_FILE_TEST_EXISTS))
    {
      file_changed_cb (priv->monitor, NULL, NULL,
                       G_FILE_MONITOR_EVENT_CREATED, self);
      return FALSE;
    }

  return TRUE;
}

static gboolean
poll_timeout_cb (ClutterMozEmbed *self)
{
  disconnect_poll_sources (self);
  return FALSE;
}

static void
clutter_mozembed_open_pipes (ClutterMozEmbed *self)
{
  gint fd;
  GFile *file;

  GError *error = NULL;
  ClutterMozEmbedPrivate *priv = self->priv;

  /* FIXME: This needs a time-out, or we can block here indefinitely if the
   *        backend crashes.
   */

  /* Wait for headless process to create the output pipe */
  file = g_file_new_for_path (priv->output_file);
  priv->monitor = g_file_monitor_file (file, 0, NULL, &error);
  if (!priv->monitor)
    {
      g_warning ("Error creating file monitor: %s", error->message);
      g_error_free (error);
    }
  g_object_unref (file);
  g_signal_connect (priv->monitor, "changed",
                    G_CALLBACK (file_changed_cb), self);

  if (g_file_test (priv->output_file, G_FILE_TEST_EXISTS))
    file_changed_cb (priv->monitor, NULL, NULL,
                     G_FILE_MONITOR_EVENT_CREATED, self);
  else
    {
      if (priv->poll_timeout)
        {
          priv->poll_source = g_timeout_add_full (G_PRIORITY_HIGH_IDLE,
                                                  50,
                                                  (GSourceFunc)poll_idle_cb,
                                                  self,
                                                  NULL);
          priv->poll_timeout_source =
            g_timeout_add (priv->poll_timeout,
                           (GSourceFunc)poll_timeout_cb,
                           self);
        }
    }

  /* Open output channel */
  mkfifo (priv->input_file, S_IWUSR | S_IRUSR);
  fd = open (priv->input_file, O_RDWR | O_NONBLOCK);
  priv->output = g_io_channel_unix_new (fd);
  g_io_channel_set_encoding (priv->output, NULL, NULL);
  g_io_channel_set_buffered (priv->output, FALSE);
  g_io_channel_set_close_on_unref (priv->output, TRUE);
}

static gboolean
connect_timeout_cb (ClutterMozEmbed *self)
{
  disconnect_poll_sources (self);
  disconnect_file_monitor_sources (self);
  g_signal_emit (self, signals[CRASHED], 0);
  return FALSE;
}

static gchar *
clutter_mozembed_strv_to_env (gchar **strv, const gchar *prefix)
{
  GString *str = g_string_new (prefix);
  gchar **p;

  for (p = strv; *p; p++)
    {
      if (p > strv)
        g_string_append_c (str, ':');
      g_string_append (str, *p);
    }

  return g_string_free (str, FALSE);
}

static gchar **
clutter_mozembed_get_paths_env (ClutterMozEmbed *self)
{
  ClutterMozEmbedPrivate *priv = self->priv;
  gchar **env_names;
  guint i, env_size;
  gchar **new_env;

  /* Copy the existing environment */
  env_names = g_listenv ();
  env_size = g_strv_length (env_names);
  new_env = g_new (gchar *, env_size + 4);

  for (i = 0; i < env_size; i++)
    new_env[i] = g_strconcat (env_names[i], "=", g_getenv (env_names[i]), NULL);

  g_strfreev (env_names);

  /* Add vars containing the list of paths */
  if (priv->comp_paths)
    new_env[i++] =
      clutter_mozembed_strv_to_env (priv->comp_paths,
                                    "CLUTTER_MOZEMBED_COMP_PATHS=");
  if (priv->chrome_paths)
    new_env[i++] =
      clutter_mozembed_strv_to_env (priv->chrome_paths,
                                    "CLUTTER_MOZEMBED_CHROME_PATHS=");

  if (priv->user_chrome_path)
    new_env[i++] =
      g_strconcat ("CLUTTER_MOZEMBED_DIRECTORIES=UChrm,",
                   priv->user_chrome_path,
                   NULL);

  new_env[i++] = NULL;

  return new_env;
}

static void
clutter_mozembed_constructed (GObject *object)
{
  static gint spawned_windows = 0;
  gboolean success;

  gchar *argv[] = {
    CMH_BIN,
    NULL, /* Output pipe */
    NULL, /* Input pipe */
    NULL, /* Private mode */
    NULL
  };

  ClutterMozEmbed *self = CLUTTER_MOZEMBED (object);
  ClutterMozEmbedPrivate *priv = self->priv;
  GError *error = NULL;

  /* Set up out-of-process renderer */

  /* Generate names for pipes, if not provided */
  /* Don't overwrite user-supplied pipe files */
  if (priv->output_file && g_file_test (priv->output_file, G_FILE_TEST_EXISTS))
    {
      g_free (priv->output_file);
      priv->output_file = NULL;
    }
  if (g_file_test (priv->input_file, G_FILE_TEST_EXISTS))
    {
      g_free (priv->input_file);
      priv->input_file = NULL;
    }

  if (!priv->output_file)
    priv->output_file = g_strdup_printf ("%s/clutter-mozembed-%d-%d",
                                         g_get_tmp_dir (), getpid (),
                                         spawned_windows);

  if (!priv->input_file)
    priv->input_file = g_strdup_printf ("%s/clutter-mozheadless-%d-%d",
                                        g_get_tmp_dir (), getpid (),
                                        spawned_windows);

  spawned_windows++;

  /* Spawn renderer */
  argv[1] = priv->output_file;
  argv[2] = priv->input_file;
  if (priv->private)
    argv[3] = "p";

  if (priv->spawn)
    {
      if (g_getenv ("CLUTTER_MOZEMBED_DEBUG"))
        {
          g_message ("Waiting for '%s %s %s %s' to be run",
                     argv[0], argv[1], argv[2],
                     priv->private ? argv[3] : "");
          priv->connect_timeout = 0;
        }
      else
        {
          gchar **env = clutter_mozembed_get_paths_env (self);

          success = g_spawn_async_with_pipes (NULL,
                                              argv,
                                              env,
                                              G_SPAWN_SEARCH_PATH /*|
                                              G_SPAWN_STDERR_TO_DEV_NULL |
                                              G_SPAWN_STDOUT_TO_DEV_NULL*/,
                                              NULL,
                                              NULL,
                                              &priv->child_pid,
                                              NULL,
                                              NULL,
                                              NULL,
                                              &error);

          g_strfreev (env);

          if (!success)
            {
              g_warning ("Error spawning renderer: %s", error->message);
              g_error_free (error);
              return;
            }
        }
    }

  if (priv->connect_timeout)
    priv->connect_timeout_source =
      g_timeout_add (priv->connect_timeout,
                     (GSourceFunc)connect_timeout_cb,
                     self);

  clutter_mozembed_open_pipes (self);
}

static void
clutter_mozembed_size_change (ClutterTexture *texture, gint width, gint height)
{
  ClutterMozEmbedPrivate *priv = CLUTTER_MOZEMBED (texture)->priv;

  if (priv->read_only)
    clutter_actor_queue_relayout (CLUTTER_ACTOR (texture));
}

static void
clutter_mozembed_class_init (ClutterMozEmbedClass *klass)
{
  GObjectClass      *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  ClutterTextureClass *texture_class = CLUTTER_TEXTURE_CLASS (klass);

  g_type_class_add_private (klass, sizeof (ClutterMozEmbedPrivate));

  object_class->get_property = clutter_mozembed_get_property;
  object_class->set_property = clutter_mozembed_set_property;
  object_class->dispose = clutter_mozembed_dispose;
  object_class->finalize = clutter_mozembed_finalize;
  object_class->constructed = clutter_mozembed_constructed;

  actor_class->allocate             = clutter_mozembed_allocate;
  actor_class->get_preferred_width  = clutter_mozembed_get_preferred_width;
  actor_class->get_preferred_height = clutter_mozembed_get_preferred_height;
  actor_class->paint                = clutter_mozembed_paint;
  actor_class->pick                 = clutter_mozembed_pick;
  actor_class->motion_event         = clutter_mozembed_motion_event;
  actor_class->button_press_event   = clutter_mozembed_button_press_event;
  actor_class->button_release_event = clutter_mozembed_button_release_event;
  actor_class->key_press_event      = clutter_mozembed_key_press_event;
  actor_class->key_release_event    = clutter_mozembed_key_release_event;
  actor_class->scroll_event         = clutter_mozembed_scroll_event;
  actor_class->key_focus_in         = clutter_mozembed_key_focus_in;
  actor_class->key_focus_out        = clutter_mozembed_key_focus_out;
#ifdef SUPPORT_PLUGINS
  actor_class->map                  = clutter_mozembed_map;
  actor_class->unmap                = clutter_mozembed_unmap;
#endif

  texture_class->size_change        = clutter_mozembed_size_change;

  g_object_class_install_property (object_class,
                                   PROP_LOCATION,
                                   g_param_spec_string ("location",
                                                        "Location",
                                                        "Current URL.",
                                                        "about:blank",
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_TITLE,
                                   g_param_spec_string ("title",
                                                        "Title",
                                                        "Current page title.",
                                                        "",
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_READONLY,
                                   g_param_spec_boolean ("read-only",
                                                         "Read-only",
                                                         "Whether to disallow "
                                                         "input.",
                                                         FALSE,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_NICK |
                                                         G_PARAM_STATIC_BLURB |
                                                         G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (object_class,
                                   PROP_INPUT,
                                   g_param_spec_string ("input",
                                                        "Input file",
                                                        "Communications pipe "
                                                        "file name (input).",
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB |
                                                        G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (object_class,
                                   PROP_OUTPUT,
                                   g_param_spec_string ("output",
                                                        "Output file",
                                                        "Communications pipe "
                                                        "file name (output).",
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB |
                                                        G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (object_class,
                                   PROP_XID,
                                   g_param_spec_ulong ("xid",
                                                       "Drawable XID",
                                                       "XID of the buffer "
                                                       "image.",
                                                       0, G_MAXULONG, 0,
                                                       G_PARAM_READABLE |
                                                       G_PARAM_STATIC_NAME |
                                                       G_PARAM_STATIC_NICK |
                                                       G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_SPAWN,
                                   g_param_spec_boolean ("spawn",
                                                         "Spawn",
                                                         "Spawn a new process.",
                                                         TRUE,
                                                         G_PARAM_WRITABLE |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_NICK |
                                                         G_PARAM_STATIC_BLURB |
                                                         G_PARAM_CONSTRUCT_ONLY));


  g_object_class_install_property (object_class,
                                   PROP_ICON,
                                   g_param_spec_string ("icon",
                                                        "Icon",
                                                        "URL to the icon "
                                                        "for this page",
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_SCROLLBARS,
                                   g_param_spec_boolean ("scrollbars",
                                                         "Show scroll-bars",
                                                         "Show scroll-bars.",
                                                         TRUE,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_NICK |
                                                         G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_ASYNC_SCROLL,
                                   g_param_spec_boolean ("async-scroll",
                                                         "Async scrolling",
                                                         "Use asynchronous "
                                                         "scrolling.",
                                                         FALSE,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_NICK |
                                                         G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_DOC_WIDTH,
                                   g_param_spec_int ("doc-width",
                                                     "Document width",
                                                     "Width of the document.",
                                                     0, G_MAXINT, 0,
                                                     G_PARAM_READABLE |
                                                     G_PARAM_STATIC_NAME |
                                                     G_PARAM_STATIC_NICK |
                                                     G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_DOC_HEIGHT,
                                   g_param_spec_int ("doc-height",
                                                     "Document height",
                                                     "Height of the document.",
                                                     0, G_MAXINT, 0,
                                                     G_PARAM_READABLE |
                                                     G_PARAM_STATIC_NAME |
                                                     G_PARAM_STATIC_NICK |
                                                     G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_SCROLL_X,
                                   g_param_spec_int ("scroll-x",
                                                     "Scroll X",
                                                     "Current X-axis scroll "
                                                     "position.",
                                                     0, G_MAXINT, 0,
                                                     G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_NAME |
                                                     G_PARAM_STATIC_NICK |
                                                     G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_SCROLL_Y,
                                   g_param_spec_int ("scroll-y",
                                                     "Scroll Y",
                                                     "Current Y-axis scroll "
                                                     "position.",
                                                     0, G_MAXINT, 0,
                                                     G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_NAME |
                                                     G_PARAM_STATIC_NICK |
                                                     G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_POLL_TIMEOUT,
                                   g_param_spec_uint ("poll-timeout",
                                                      "Poll-timeout",
                                                      "Amount of time to "
                                                      "try polling for a "
                                                      "connection (in ms).",
                                                      0, G_MAXINT, 3000,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_NAME |
                                                      G_PARAM_STATIC_NICK |
                                                      G_PARAM_STATIC_BLURB |
                                                      G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (object_class,
                                   PROP_CONNECT_TIMEOUT,
                                   g_param_spec_uint ("connect-timeout",
                                                      "Connect-timeout",
                                                      "Amount of time to "
                                                      "wait for a "
                                                      "connection (in ms).",
                                                      0, G_MAXINT, 10000,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_NAME |
                                                      G_PARAM_STATIC_NICK |
                                                      G_PARAM_STATIC_BLURB |
                                                      G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (object_class,
                                   PROP_CAN_GO_BACK,
                                   g_param_spec_boolean ("can-go-back",
                                                         "Can go back",
                                                         "Whether backwards "
                                                         "navigation is "
                                                         "possible.",
                                                         FALSE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_NICK |
                                                         G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_CAN_GO_FORWARD,
                                   g_param_spec_boolean ("can-go-forward",
                                                         "Can go forward",
                                                         "Whether forwards "
                                                         "navigation is "
                                                         "possible.",
                                                         FALSE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_NICK |
                                                         G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_CURSOR,
                                   g_param_spec_uint ("cursor",
                                                      "Cursor",
                                                      "Cursor type",
                                                      0, G_MAXINT, 0,
                                                      G_PARAM_READABLE |
                                                      G_PARAM_STATIC_NAME |
                                                      G_PARAM_STATIC_NICK |
                                                      G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_SECURITY,
                                   g_param_spec_uint ("security",
                                                      "Security status",
                                                      "Presence of secure web "
                                                      "elements and level of "
                                                      "security",
                                                      0, G_MAXUINT, 0,
                                                      G_PARAM_READABLE |
                                                      G_PARAM_STATIC_NAME |
                                                      G_PARAM_STATIC_NICK |
                                                      G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_COMP_PATHS,
                                   g_param_spec_boxed ("comp-paths",
                                                       "Component Paths",
                                                       "Array of additional "
                                                       "paths to search for "
                                                       "components.",
                                                       G_TYPE_STRV,
                                                       G_PARAM_READWRITE |
                                                       G_PARAM_STATIC_NAME |
                                                       G_PARAM_STATIC_NICK |
                                                       G_PARAM_STATIC_BLURB |
                                                       G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (object_class,
                                   PROP_CHROME_PATHS,
                                   g_param_spec_boxed ("chrome-paths",
                                                       "Chrome Paths",
                                                       "Array of additional "
                                                       "paths to search for "
                                                       "chrome manifests.",
                                                       G_TYPE_STRV,
                                                       G_PARAM_READWRITE |
                                                       G_PARAM_STATIC_NAME |
                                                       G_PARAM_STATIC_NICK |
                                                       G_PARAM_STATIC_BLURB |
                                                       G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (object_class,
                                   PROP_PRIVATE,
                                   g_param_spec_boolean ("private",
                                                         "Private",
                                                         "Whether to store "
                                                         "any persistent data.",
                                                         FALSE,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_NICK |
                                                         G_PARAM_STATIC_BLURB |
                                                         G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (object_class,
                                   PROP_USER_CHROME_PATH,
                                   g_param_spec_string ("user-chrome-path",
                                                        "User-chrome path",
                                                        "User-chrome search "
                                                        "path.",
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB |
                                                        G_PARAM_CONSTRUCT_ONLY));

  signals[PROGRESS] =
    g_signal_new ("progress",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterMozEmbedClass, progress),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__DOUBLE,
                  G_TYPE_NONE, 1, G_TYPE_DOUBLE);

  signals[NET_START] =
    g_signal_new ("net-start",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterMozEmbedClass, net_start),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[NET_STOP] =
    g_signal_new ("net-stop",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterMozEmbedClass, net_stop),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[CRASHED] =
    g_signal_new ("crashed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterMozEmbedClass, crashed),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[NEW_WINDOW] =
    g_signal_new ("new-window",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterMozEmbedClass, new_window),
                  NULL, NULL,
                  _clutter_mozembed_marshal_VOID__POINTER_UINT,
                  G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_UINT);

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterMozEmbedClass, closed),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[LINK_MESSAGE] =
    g_signal_new ("link-message",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterMozEmbedClass, link_message),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__STRING,
                  G_TYPE_NONE, 1, G_TYPE_STRING);

  signals[SIZE_REQUEST] =
    g_signal_new ("size-request",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterMozEmbedClass, size_request),
                  NULL, NULL,
                  _clutter_mozembed_marshal_VOID__INT_INT,
                  G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);

  signals[DOWNLOAD] =
    g_signal_new ("download",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterMozEmbedClass, download),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1, CLUTTER_TYPE_MOZEMBED_DOWNLOAD);

  signals[SHOW_TOOLTIP] =
    g_signal_new ("show-tooltip",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterMozEmbedClass, show_tooltip),
                  NULL, NULL,
                  _clutter_mozembed_marshal_VOID__STRING_INT_INT,
                  G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT);

  signals[HIDE_TOOLTIP] =
    g_signal_new ("hide-tooltip",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterMozEmbedClass, hide_tooltip),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[CONTEXT_INFO] =
    g_signal_new ("context-info",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterMozEmbedClass, context_info),
                  NULL, NULL,
                  _clutter_mozembed_marshal_VOID__UINT_STRING_STRING_STRING_STRING,
                  G_TYPE_NONE, 5, G_TYPE_UINT,
                  G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
}

static void
_destroy_download_cb (gpointer data)
{
  g_signal_handlers_disconnect_matched (data,
                                        G_SIGNAL_MATCH_FUNC,
                                        0,
                                        0,
                                        NULL,
                                        _download_finished_cb,
                                        NULL);
  g_object_unref (G_OBJECT (data));
}

#ifdef SUPPORT_IM
static void
clutter_mozembed_imcontext_commit_cb(ClutterIMContext *context,
                                     const gchar      *str,
                                     ClutterMozEmbed  *mozembed)
{
  clutter_mozembed_comms_send (mozembed->priv->output,
                               CME_COMMAND_IM_COMMIT,
                               G_TYPE_STRING,  str,
                               G_TYPE_INVALID);
}

static void
clutter_mozembed_imcontext_preedit_changed_cb (ClutterIMContext *context,
                                               ClutterMozEmbed  *mozembed)
{
  gchar *str;
  PangoAttrList *attrs;
  gint cursor_pos;

  clutter_im_context_get_preedit_string (context,
                                         &str,
                                         &attrs,
                                         &cursor_pos);

  clutter_mozembed_comms_send (mozembed->priv->output,
                               CME_COMMAND_IM_PREEDIT_CHANGED,
                               G_TYPE_STRING, str,
                               G_TYPE_INT, cursor_pos,
                               G_TYPE_INVALID);
  g_free(str);
  pango_attr_list_unref(attrs);
}
#endif

static void
clutter_mozembed_init (ClutterMozEmbed *self)
{

  ClutterMozEmbedPrivate *priv = self->priv = MOZEMBED_PRIVATE (self);

  priv->motion_ack = TRUE;
  priv->scroll_ack = TRUE;
  priv->spawn = TRUE;
  priv->poll_timeout = 3000;
  priv->connect_timeout = 10000;
  priv->downloads = g_hash_table_new_full (g_direct_hash,
                                           g_direct_equal,
                                           NULL,
                                           _destroy_download_cb);
  priv->scrollbars = TRUE;
  priv->is_loading = TRUE;

  clutter_actor_set_reactive (CLUTTER_ACTOR (self), TRUE);

  /* Turn off sync-size (we manually size the texture on allocate) and turn
   * on automatic tfp updates.
   */
  g_object_set (G_OBJECT (self),
                "sync-size", FALSE,
                "automatic-updates", TRUE,
                "disable-slicing", TRUE,
                "filter-quality", CLUTTER_TEXTURE_QUALITY_HIGH,
                NULL);

#ifdef SUPPORT_IM
  priv->im_enabled = FALSE;

  priv->im_context = clutter_im_multicontext_new ();
  priv->im_context->actor = CLUTTER_ACTOR (self);

  g_signal_connect (priv->im_context, "commit",
                    G_CALLBACK (clutter_mozembed_imcontext_commit_cb),
                    self);
  g_signal_connect (priv->im_context, "preedit-changed",
                    G_CALLBACK (clutter_mozembed_imcontext_preedit_changed_cb),
                    self);
#endif
  priv->plugin_windows = NULL;
  priv->plugin_viewport = NULL;
  priv->layout_container = NULL;
  priv->plugin_viewport_initialized = FALSE;
}

ClutterActor *
clutter_mozembed_new (void)
{
  return CLUTTER_ACTOR (g_object_new (CLUTTER_TYPE_MOZEMBED, NULL));
}

ClutterActor *
clutter_mozembed_new_with_parent (ClutterMozEmbed *parent)
{
  gchar *input, *output;
  ClutterActor *mozembed;

  if (!parent)
    return clutter_mozembed_new ();

  /* Open up a new window using the same process as the provided
   * ClutterMozEmbed.
   */
  mozembed = g_object_new (CLUTTER_TYPE_MOZEMBED, "spawn", FALSE, NULL);
  g_object_get (G_OBJECT (mozembed),
                "input", &input,
                "output", &output,
                NULL);
  CLUTTER_MOZEMBED (mozembed)->priv->private = parent->priv->private;

  clutter_mozembed_comms_send (parent->priv->output,
                               CME_COMMAND_NEW_WINDOW,
                               G_TYPE_STRING, input,
                               G_TYPE_STRING, output,
                               G_TYPE_INVALID);

  g_free (input);
  g_free (output);

  return mozembed;
}

ClutterActor *
clutter_mozembed_new_for_new_window ()
{
  return g_object_new (CLUTTER_TYPE_MOZEMBED, "spawn", FALSE, NULL);
}

ClutterActor *
clutter_mozembed_new_view (void)
{
  ClutterMozEmbed *mozembed;

  /* Create a read-only mozembed */
  mozembed = g_object_new (CLUTTER_TYPE_MOZEMBED,
                           "read-only", TRUE,
                           "spawn", FALSE,
                           NULL);

  return CLUTTER_ACTOR (mozembed);
}

void
clutter_mozembed_connect_view (ClutterMozEmbed *mozembed,
                               const gchar     *input,
                               const gchar     *output)
{
  clutter_mozembed_comms_send (mozembed->priv->output,
                               CME_COMMAND_NEW_VIEW,
                               G_TYPE_STRING, input,
                               G_TYPE_STRING, output,
                               G_TYPE_INVALID);
}

void
clutter_mozembed_open (ClutterMozEmbed *mozembed, const gchar *uri)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;

  clutter_mozembed_comms_send (priv->output,
                               CME_COMMAND_OPEN_URL,
                               G_TYPE_STRING, uri,
                               G_TYPE_INVALID);
}

const gchar *
clutter_mozembed_get_location (ClutterMozEmbed *mozembed)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;
  return priv->location;
}

const gchar *
clutter_mozembed_get_title (ClutterMozEmbed *mozembed)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;
  return priv->title;
}

const gchar *
clutter_mozembed_get_icon (ClutterMozEmbed *mozembed)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;
  return priv->icon;
}

gboolean
clutter_mozembed_can_go_back (ClutterMozEmbed *mozembed)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;
  return priv->can_go_back;
}

gboolean
clutter_mozembed_can_go_forward (ClutterMozEmbed *mozembed)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;
  return priv->can_go_forward;
}

void
clutter_mozembed_back (ClutterMozEmbed *mozembed)
{
  clutter_mozembed_comms_send (mozembed->priv->output,
                               CME_COMMAND_BACK,
                               G_TYPE_INVALID);
}

void
clutter_mozembed_forward (ClutterMozEmbed *mozembed)
{
  clutter_mozembed_comms_send (mozembed->priv->output,
                               CME_COMMAND_FORWARD,
                               G_TYPE_INVALID);
}

void
clutter_mozembed_stop (ClutterMozEmbed *mozembed)
{
  clutter_mozembed_comms_send (mozembed->priv->output,
                               CME_COMMAND_STOP,
                               G_TYPE_INVALID);
}

void
clutter_mozembed_refresh (ClutterMozEmbed *mozembed)
{
  clutter_mozembed_comms_send (mozembed->priv->output,
                               CME_COMMAND_REFRESH,
                               G_TYPE_INVALID);
}

void
clutter_mozembed_reload (ClutterMozEmbed *mozembed)
{
  clutter_mozembed_comms_send (mozembed->priv->output,
                               CME_COMMAND_RELOAD,
                               G_TYPE_INVALID);
}

void
clutter_mozembed_request_close (ClutterMozEmbed *mozembed)
{
  clutter_mozembed_comms_send (mozembed->priv->output,
                               CME_COMMAND_CLOSE,
                               G_TYPE_INVALID);
}

void
clutter_mozembed_purge_session_history (ClutterMozEmbed *mozembed)
{
  clutter_mozembed_comms_send (mozembed->priv->output,
                               CME_COMMAND_PURGE_SESSION_HISTORY,
                               G_TYPE_INVALID);
}

gboolean
clutter_mozembed_get_async_scroll (ClutterMozEmbed *mozembed)
{
  return mozembed->priv->async_scroll;
}

void
clutter_mozembed_set_async_scroll (ClutterMozEmbed *mozembed, gboolean async)
{
  mozembed->priv->async_scroll = async;
}

gboolean
clutter_mozembed_get_scrollbars (ClutterMozEmbed *mozembed)
{
  return mozembed->priv->scrollbars;
}

void
clutter_mozembed_set_scrollbars (ClutterMozEmbed *mozembed, gboolean show)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;

  if (priv->scrollbars != show)
    {
      priv->scrollbars = show;
      clutter_mozembed_comms_send (priv->output,
                                   CME_COMMAND_TOGGLE_CHROME,
                                   G_TYPE_INT, MOZ_HEADLESS_FLAG_SCROLLBARSON,
                                   G_TYPE_INVALID);
    }
}

void
clutter_mozembed_scroll_by (ClutterMozEmbed *mozembed, gint dx, gint dy)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;

  clutter_mozembed_comms_send (priv->output,
                               CME_COMMAND_SCROLL,
                               G_TYPE_INT, dx,
                               G_TYPE_INT, dy,
                               G_TYPE_INVALID);

  priv->offset_x -= dx;
  priv->offset_y -= dy;

  clamp_offset (mozembed);

  if (priv->async_scroll)
    clutter_actor_queue_redraw (CLUTTER_ACTOR (mozembed));
}

void
clutter_mozembed_scroll_to (ClutterMozEmbed *mozembed, gint x, gint y)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;

  if (priv->scroll_ack)
    {
      clutter_mozembed_comms_send (priv->output,
                                   CME_COMMAND_SCROLL_TO,
                                   G_TYPE_INT, x,
                                   G_TYPE_INT, y,
                                   G_TYPE_INVALID);
      priv->scroll_ack = FALSE;
    }
  else
    {
      priv->pending_scroll = TRUE;
      priv->pending_scroll_x = x;
      priv->pending_scroll_y = y;
    }

  /* TODO: Check that these two lines are correct */
  priv->offset_x -= x - (priv->scroll_x + priv->offset_y);
  priv->offset_y -= y - (priv->scroll_y + priv->offset_y);

  clamp_offset (mozembed);

  if (priv->async_scroll)
    clutter_actor_queue_redraw (CLUTTER_ACTOR (mozembed));
}

gboolean
clutter_mozembed_is_loading (ClutterMozEmbed *mozembed)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;
  return priv->is_loading;
}

gdouble
clutter_mozembed_get_progress (ClutterMozEmbed *mozembed)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;
  return priv->progress;
}

MozHeadlessCursorType
clutter_mozembed_get_cursor (ClutterMozEmbed *mozembed)
{
  return mozembed->priv->cursor;
}

guint
clutter_mozembed_get_security (ClutterMozEmbed *mozembed)
{
  return mozembed->priv->security;
}

GList *
clutter_mozembed_get_downloads (ClutterMozEmbed *mozembed)
{
  return g_hash_table_get_values (mozembed->priv->downloads);
}

void
clutter_mozembed_save_uri (ClutterMozEmbed *mozembed,
                           const gchar     *uri,
                           const gchar     *target)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;

  clutter_mozembed_comms_send (priv->output,
                               CME_COMMAND_DL_CREATE,
                               G_TYPE_STRING, uri,
                               G_TYPE_STRING, target,
                               G_TYPE_INVALID);
}

gboolean
clutter_mozembed_get_private (ClutterMozEmbed *mozembed)
{
  return mozembed->priv->private;
}

void
clutter_mozembed_lower (ClutterMozEmbed *mozembed)
{
#ifdef SUPPORT_PLUGINS
  /* we don't know whether this mozembed is obscured by
     others. If it is, the plugin_viewport window need to
     be lowered. */
  ClutterMozEmbedPrivate *priv = mozembed->priv;
  GdkWindow              *gdk_window;

  if (!priv->plugin_viewport_initialized)
    return;

  gdk_window = gtk_widget_get_window (priv->plugin_viewport);

  gdk_window_lower (gdk_window);
#endif
}

void
clutter_mozembed_raise (ClutterMozEmbed *mozembed)
{
#ifdef SUPPORT_PLUGINS
  ClutterMozEmbedPrivate *priv = mozembed->priv;
  GdkWindow              *gdk_window;

  if(!priv->plugin_viewport_initialized)
    return;

  gdk_window = gtk_widget_get_window(priv->plugin_viewport);

  gdk_window_raise (gdk_window);
#endif
}

void
clutter_mozembed_set_layout_container (ClutterMozEmbed *mozembed,
                                       GtkWidget       *container)
{
#ifdef SUPPORT_PLUGINS
  ClutterMozEmbedPrivate *priv = mozembed->priv;

  priv->layout_container = container;
#endif
}

void
clutter_mozembed_set_search_string (ClutterMozEmbed *mozembed,
                                    const gchar     *string)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;

  clutter_mozembed_comms_send (priv->output,
                               CME_COMMAND_SET_SEARCH_STRING,
                               G_TYPE_STRING, string,
                               G_TYPE_INVALID);
}

void
clutter_mozembed_find_next (ClutterMozEmbed *mozembed)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;

  clutter_mozembed_comms_send (priv->output,
                               CME_COMMAND_FIND_NEXT,
                               G_TYPE_INVALID);
}

void
clutter_mozembed_find_prev (ClutterMozEmbed *mozembed)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;

  clutter_mozembed_comms_send (priv->output,
                               CME_COMMAND_FIND_PREV,
                               G_TYPE_INVALID);
}

void
clutter_mozembed_set_transparent (ClutterMozEmbed *mozembed,
                                  gboolean         transparent)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;

  clutter_mozembed_comms_send (priv->output,
                               CME_COMMAND_SET_TRANSPARENT,
                               G_TYPE_BOOLEAN, transparent,
                               G_TYPE_INVALID);
}

