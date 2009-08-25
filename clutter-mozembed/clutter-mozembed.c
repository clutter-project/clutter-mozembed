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


G_DEFINE_TYPE (ClutterMozEmbed, clutter_mozembed, CLUTTER_TYPE_TEXTURE)

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
  PROP_SHM,
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
  PROP_PRIVATE
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

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

static void clutter_mozembed_open_pipes (ClutterMozEmbed *self);
static MozHeadlessModifier
  clutter_mozembed_get_modifier (ClutterModifierType modifiers);

#ifdef SUPPORT_PLUGINS
typedef struct _PluginWindow
{
  gint          x, y;
  ClutterActor *plugin_tfp;
} PluginWindow;

static void
clutter_mozembed_allocate_plugins (ClutterMozEmbed        *mozembed,
                                   ClutterAllocationFlags  flags);

static gboolean
clutter_mozembed_init_viewport (ClutterMozEmbed *mozembed);

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
        gint             x,
        gint             y,
        gint             width,
        gint             height,
        gint             surface_width,
        gint             surface_height)
{
  gboolean result;
  float tex_width, tex_height;

  ClutterMozEmbedPrivate *priv = self->priv;
  GError *error = NULL;

  if (!priv->opened_shm)
    {
      /*g_debug ("Opening shm");*/
      priv->opened_shm = TRUE;
      priv->shm_fd = shm_open (priv->shm_name, O_RDONLY, 0444);
      if (priv->shm_fd == -1)
        g_error ("Error opening shared memory");
    }

  clutter_actor_get_size (CLUTTER_ACTOR (self), &tex_width, &tex_height);

  /* If the surface size of the mozilla window is different to our texture 
   * size, ignore it - it just means we've resized in the middle of the
   * backend drawing and we'll get a new update almost immediately anyway.
   */
  if (!priv->read_only)
    if ((surface_width != (gint)tex_width) ||
        (surface_height != (gint)tex_height))
      return;

  /* Watch out for a resize of the source data, only happens for read-only */
  if (priv->image_data &&
      (priv->image_size != surface_width * surface_height * 4))
    {
      munmap (priv->image_data, priv->image_size);
      priv->image_data = NULL;
    }

  if (!priv->image_data)
    {
      /*g_debug ("mmapping data");*/
      priv->image_size = surface_width * surface_height * 4;
      priv->image_data = mmap (NULL, priv->image_size, PROT_READ,
                               MAP_SHARED, priv->shm_fd, 0);

      if (priv->image_data == MAP_FAILED)
        {
          g_warning ("Unable to mmap image data\n");
          priv->image_data = NULL;
          return;
        }
    }

  /*g_debug ("Reading data");*/

  /* Copy data to texture */
  if ((x == 0) && (y == 0) && (width == surface_width) &&
      (height == surface_height))
    result = clutter_texture_set_from_rgb_data (CLUTTER_TEXTURE (self),
                                                priv->image_data,
                                                TRUE,
                                                surface_width,
                                                surface_height,
                                                surface_width * 4,
                                                4,
                                                CLUTTER_TEXTURE_RGB_FLAG_BGR |
                                                CLUTTER_TEXTURE_RGB_FLAG_PREMULT,
                                                &error);
  else
    result = clutter_texture_set_area_from_rgb_data (CLUTTER_TEXTURE (self),
                                                     priv->image_data + (x*4) +
                                                     (y*surface_width*4),
                                                     TRUE,
                                                     x,
                                                     y,
                                                     width,
                                                     height,
                                                     surface_width * 4,
                                                     4,
                                                     CLUTTER_TEXTURE_RGB_FLAG_BGR |
                                                     CLUTTER_TEXTURE_RGB_FLAG_PREMULT,
                                                     &error);

  if (!result)
    {
      g_warning ("Error setting texture data: %s", error->message);
      g_error_free (error);
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
        gint x, y, width, height, surface_width, surface_height,
             doc_width, doc_height, scroll_x, scroll_y;

        clutter_mozembed_comms_receive (priv->input,
                                        G_TYPE_INT, &x,
                                        G_TYPE_INT, &y,
                                        G_TYPE_INT, &width,
                                        G_TYPE_INT, &height,
                                        G_TYPE_INT, &surface_width,
                                        G_TYPE_INT, &surface_height,
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

        update (self, x, y, width, height, surface_width, surface_height);

        priv->repaint_id =
          clutter_threads_add_repaint_func ((GSourceFunc)
                                            clutter_mozembed_repaint_func,
                                            self,
                                            NULL);
        clutter_actor_queue_redraw (CLUTTER_ACTOR (self));
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
            gchar *output_file, *input_file, *shm_name;

            output_file = input_file = shm_name = NULL;
            g_object_get (G_OBJECT (new_window),
                          "output", &output_file,
                          "input", &input_file,
                          "shm", &shm_name,
                          NULL);

            clutter_mozembed_comms_send (priv->output,
                                         CME_COMMAND_NEW_WINDOW_RESPONSE,
                                         G_TYPE_BOOLEAN, TRUE,
                                         G_TYPE_STRING, input_file,
                                         G_TYPE_STRING, output_file,
                                         G_TYPE_STRING, shm_name,
                                         G_TYPE_INVALID);

            g_free (output_file);
            g_free (input_file);
            g_free (shm_name);
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
    case CME_FEEDBACK_SHM_NAME :
      {
        g_free (priv->shm_name);
        priv->shm_name = clutter_mozembed_comms_receive_string (priv->input);
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
    case CME_FEEDBACK_PLUGIN_VIEWPORT :
      {
#ifdef SUPPORT_PLUGINS
        unsigned long window =
          clutter_mozembed_comms_receive_ulong (priv->input);

        if (priv->plugin_viewport != window)
          {
            priv->plugin_viewport = window;
            clutter_mozembed_init_viewport (self);
          }
#endif
        break;
      }
#ifdef SUPPORT_IM
    case CME_FEEDBACK_IM_RESET:
      {
        if (!priv->im_enabled)
          break;

        clutter_im_context_reset (priv->im_context);

        break;
      }
    case CME_FEEDBACK_IM_ENABLE:
      {
        priv->im_enabled = clutter_mozembed_comms_receive_boolean (priv->input);

        break;
      }
    case CME_FEEDBACK_IM_FOCUS_CHANGE:
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
    case CME_FEEDBACK_IM_SET_CURSOR:
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
find_plugin_window (GList *plugins, Window xwindow)
{
  GList *pwin;
  for (pwin = plugins; pwin != NULL; pwin = pwin->next)
    {
      PluginWindow *plugin_window = pwin->data;
      Window        plugin_xwindow;

#ifndef DEBUG_PLUGIN_VIEWPORT
      g_object_get (G_OBJECT (plugin_window->plugin_tfp),
                    "window", &plugin_xwindow,
                    NULL);
#else
      return plugin_window;
#endif

      if (plugin_xwindow == xwindow)
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

  if (!priv->plugin_viewport)
    return;

  mapped = CLUTTER_ACTOR_IS_MAPPED (mozembed);
  reactive = CLUTTER_ACTOR_IS_REACTIVE (mozembed);

  clutter_actor_get_allocation_geometry (CLUTTER_ACTOR (mozembed), &geom);

  clutter_mozembed_trap_x_errors ();

  if (mapped && reactive)
    {
      clutter_actor_get_transformed_position (CLUTTER_ACTOR (mozembed),
                                              &abs_x, &abs_y);

      XMoveWindow (xdpy, priv->plugin_viewport, (int)abs_x, (int)abs_y);

      /* Note, we cover the entire area of the page with the plugin window,
       * but plugins aren't clipped currently, so they can end up overlapping
       * XUL scrollbars. This is a bug in the backend though.
       */
      if (geom.width > 0 && geom.height > 0)
        {
#ifndef DEBUG_PLUGIN_VIEWPORT
          XResizeWindow (xdpy, priv->plugin_viewport,
                         geom.width, geom.height);
#else /* XXX: Handy when disabling redirection of the viewport
         for debugging... */
          XResizeWindow (xdpy, priv->plugin_viewport,
                         geom.width - 200, geom.height - 200);
#endif
        }
    }
  else
    {
      /* Note we don't map/unmap the window since that would conflict with
       * xcomposite and live previews of plugins windows for hidden tabs */
      XMoveWindow (xdpy, priv->plugin_viewport, -geom.width, 0);
    }

  XSync (xdpy, False);
  clutter_mozembed_untrap_x_errors ();
}

static GdkFilterReturn
plugin_viewport_x_event_filter (GdkXEvent *gdk_xevent,
                                GdkEvent *event,
                                gpointer data)
{
  ClutterMozEmbed        *mozembed = CLUTTER_MOZEMBED (data);
  ClutterMozEmbedPrivate *priv = mozembed->priv;
  XEvent                 *xev = (XEvent *)gdk_xevent;
  Display                *xdpy = clutter_x11_get_default_display ();
  PluginWindow           *plugin_window;
  XWindowAttributes       attribs;
  Window                  parent_win;
  Window                  root_win;
  Window                 *children;
  unsigned int            n_children;
  Status                  status;

  switch (xev->type)
    {
    case MapNotify:

      clutter_mozembed_trap_x_errors ();
      status = XQueryTree (xdpy,
                      xev->xmap.window,
                      &root_win,
                      &parent_win,
                      &children,
                      &n_children);
      clutter_mozembed_untrap_x_errors ();
      if (status == 0)
        break;
      XFree (children);

      if (parent_win != priv->plugin_viewport)
        break;

      clutter_mozembed_trap_x_errors ();
      status = XGetWindowAttributes (xdpy, xev->xmap.window, &attribs);
      clutter_mozembed_untrap_x_errors ();
      if (status == 0)
        break;

      plugin_window = g_slice_new (PluginWindow);
      plugin_window->x = attribs.x;
      plugin_window->y = attribs.y;
#ifndef DEBUG_PLUGIN_VIEWPORT
      plugin_window->plugin_tfp =
        clutter_glx_texture_pixmap_new_with_window (xev->xmap.window);

      clutter_x11_texture_pixmap_set_automatic (
              CLUTTER_X11_TEXTURE_PIXMAP (plugin_window->plugin_tfp),
              TRUE);

      /* We don't want the parent (viewport) window to be automatically
       * updated with changes to the plugin window... */
      g_object_set (G_OBJECT (plugin_window->plugin_tfp),
                    "window-redirect-automatic",
                    FALSE,
                    NULL);
#else
      plugin_window->plugin_tfp = clutter_rectangle_new ();
      clutter_actor_set_size (plugin_window->plugin_tfp, 100, 100);
#endif

      clutter_actor_set_parent (plugin_window->plugin_tfp,
                                CLUTTER_ACTOR (mozembed));

      priv->plugin_windows =
        g_list_prepend (priv->plugin_windows, plugin_window);
      clutter_actor_queue_relayout (CLUTTER_ACTOR (mozembed));
      break;

    case UnmapNotify:
      plugin_window =
        find_plugin_window (priv->plugin_windows, xev->xunmap.window);
      if (!plugin_window)
        break;

      priv->plugin_windows =
        g_list_remove (priv->plugin_windows, plugin_window);
      clutter_actor_unparent (plugin_window->plugin_tfp);
      g_slice_free (PluginWindow, plugin_window);
      break;

    case ConfigureNotify:
      plugin_window =
        find_plugin_window (priv->plugin_windows, xev->xconfigure.window);
      if (!plugin_window)
        break;

      plugin_window->x = xev->xconfigure.x;
      plugin_window->y = xev->xconfigure.y;
      
      clutter_mozembed_sync_plugin_viewport_pos (mozembed);
      clutter_mozembed_allocate_plugins (mozembed, FALSE);
      break;
    }

  return  GDK_FILTER_CONTINUE;
}

static void
reactive_change_cb (GObject    *object,
                    GParamSpec *param_spec,
                    gpointer    data)
{
  clutter_mozembed_sync_plugin_viewport_pos (CLUTTER_MOZEMBED (object));
}

static gboolean
clutter_mozembed_init_viewport (ClutterMozEmbed *mozembed)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;
  Display *xdpy = clutter_x11_get_default_display ();
  ClutterActor *stage;
  //unsigned long pixel;
  int composite_major, composite_minor;

  if (priv->plugin_viewport_initialized)
    return TRUE;

  if (!priv->plugin_viewport)
    return FALSE;

  if (priv->read_only)
    return FALSE;

#ifdef DEBUG_PLUGIN_VIEWPORT
  XSynchronize (xdpy, True);
#endif

  stage = clutter_actor_get_stage (CLUTTER_ACTOR (mozembed));
  if (!stage || !CLUTTER_ACTOR_IS_REALIZED (stage))
    return FALSE;

  priv->stage_xwin = clutter_x11_get_stage_window (CLUTTER_STAGE (stage));
  if ((priv->stage_xwin == None) ||
      (priv->stage_xwin == clutter_x11_get_root_window()))
    return FALSE;

  priv->stage_gdk_window = gdk_window_foreign_new (priv->stage_xwin);
  if (!priv->stage_gdk_window)
    return FALSE;

  /* Initially the plugin viewport window will be parented on the root window,
   * so we need to reparent it onto the stage so that we can align the plugin
   * X windows with the corresponding ClutterGLXTexturePixmap actors. */
  XReparentWindow (xdpy, priv->plugin_viewport, priv->stage_xwin, -100, -100);

  /* Note, unlike the individual plugin windows which we redirect using clutter,
   * we redirect the viewport window directly since we want to be sure we never
   * name a pixmap for it. NB: The viewport window is only needed for input
   * clipping.
   */
  if (XCompositeQueryVersion (xdpy, &composite_major, &composite_minor)
      != True)
    {
      g_critical ("The composite extension is required for redirecting "
                  "plugin windows");
    }
#ifndef DEBUG_PLUGIN_VIEWPORT
  XCompositeRedirectWindow (xdpy,
                            priv->plugin_viewport,
                            CompositeRedirectManual);
#endif
  XSelectInput (xdpy, priv->plugin_viewport,
                SubstructureNotifyMask);

  gdk_window_add_filter (NULL,
                         plugin_viewport_x_event_filter,
                         (gpointer)mozembed);

  /* We aim to close down gracefully, but if we fail to do so we must at
   * least ensure that we don't destroy the plugin_viewport window since
   * that would most likely cause the backend moz-headless process to crash.
   */
  XAddToSaveSet (xdpy, priv->plugin_viewport);

  XMapWindow (xdpy, priv->plugin_viewport);

  /* Make sure the window is mapped, or we can end up with unparented
   * plugin windows
   */
  XSync (xdpy, False);

  clutter_mozembed_sync_plugin_viewport_pos (mozembed);

  g_signal_connect (G_OBJECT (mozembed),
                    "notify::reactive",
                    G_CALLBACK (reactive_change_cb),
                    NULL);

  priv->plugin_viewport_initialized = TRUE;

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

  clutter_mozembed_init_viewport (mozembed);

  for (p = priv->plugin_windows; p; p = p->next)
    {
      PluginWindow *window = p->data;
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

  case PROP_SHM :
    g_value_set_string (value, self->priv->shm_name);
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

  case PROP_SHM :
    priv->shm_name = g_value_dup_string (value);
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
  Display                *xdpy = clutter_x11_get_default_display ();

  while (priv->plugin_windows)
    {
      PluginWindow *plugin_window = priv->plugin_windows->data;
      clutter_actor_unparent (plugin_window->plugin_tfp);
      g_slice_free (PluginWindow, plugin_window);
      priv->plugin_windows = g_list_delete_link (priv->plugin_windows,
                                                 priv->plugin_windows);
    }

  if (priv->plugin_viewport_initialized)
    {
      XWindowAttributes  attribs;
      int width = 1000;
      int height = 1000;

      gdk_window_remove_filter (NULL,
                                plugin_viewport_x_event_filter,
                                (gpointer)object);

      clutter_mozembed_trap_x_errors ();

      if (XGetWindowAttributes (xdpy, priv->plugin_viewport, &attribs) != 0)
        {
          width = attribs.width;
          height = attribs.height;
        }

      XSync (xdpy, False);

      XUnmapWindow (xdpy, priv->plugin_viewport);
      XReparentWindow (xdpy,
                       priv->plugin_viewport,
                       XDefaultRootWindow (xdpy),
                       -width,
                       -height);
      XRemoveFromSaveSet (xdpy, priv->plugin_viewport);
      XSync (xdpy, False);
      clutter_mozembed_untrap_x_errors ();

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
  g_free (priv->shm_name);

  g_strfreev (priv->comp_paths);
  g_strfreev (priv->chrome_paths);

  if (priv->image_data)
    munmap (priv->image_data, priv->image_size);

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

  if ((!priv->read_only) && ((tex_width != width) || (tex_height != height)))
    {
      /* Unmap previous texture data */
      if (priv->image_data)
        {
          munmap (priv->image_data, priv->image_size);
          priv->image_data = NULL;
        }

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

      if (CLUTTER_ACTOR_IS_MAPPED (plugin_tfp))
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

static gboolean
clutter_mozembed_get_keyval (ClutterKeyEvent *event, guint *keyval)
{
  switch (event->keyval)
    {
    case CLUTTER_Cancel :
      *keyval = MOZ_KEY_CANCEL;
      return TRUE;
    case CLUTTER_Help :
      *keyval = MOZ_KEY_HELP;
      return TRUE;
    case CLUTTER_BackSpace :
      *keyval = MOZ_KEY_BACK_SPACE;
      return TRUE;
    case CLUTTER_Tab :
    case CLUTTER_ISO_Left_Tab :
      *keyval = MOZ_KEY_TAB;
      return TRUE;
    case CLUTTER_Return :
      *keyval = MOZ_KEY_RETURN;
      return TRUE;
    case CLUTTER_KP_Enter :
      *keyval = MOZ_KEY_ENTER;
      return TRUE;
    case CLUTTER_Shift_L :
    case CLUTTER_Shift_R :
      *keyval = MOZ_KEY_SHIFT;
      return TRUE;
    case CLUTTER_Control_L :
    case CLUTTER_Control_R :
      *keyval = MOZ_KEY_CONTROL;
      return TRUE;
    case CLUTTER_Alt_L :
    case CLUTTER_Alt_R :
      *keyval = MOZ_KEY_ALT;
      return TRUE;
    case CLUTTER_Pause :
      *keyval = MOZ_KEY_PAUSE;
      return TRUE;
    case CLUTTER_Caps_Lock :
      *keyval = MOZ_KEY_CAPS_LOCK;
      return TRUE;
    case CLUTTER_Escape :
      *keyval = MOZ_KEY_ESCAPE;
      return TRUE;
    case CLUTTER_space :
      *keyval = MOZ_KEY_SPACE;
      return TRUE;
    case CLUTTER_Page_Up :
      *keyval = MOZ_KEY_PAGE_UP;
      return TRUE;
    case CLUTTER_Page_Down :
      *keyval = MOZ_KEY_PAGE_DOWN;
      return TRUE;
    case CLUTTER_End :
      *keyval = MOZ_KEY_END;
      return TRUE;
    case CLUTTER_Home :
      *keyval = MOZ_KEY_HOME;
      return TRUE;
    case CLUTTER_Left:
      *keyval = MOZ_KEY_LEFT;
      return TRUE;
    case CLUTTER_Up:
      *keyval = MOZ_KEY_UP;
      return TRUE;
    case CLUTTER_Right:
      *keyval = MOZ_KEY_RIGHT;
      return TRUE;
    case CLUTTER_Down:
      *keyval = MOZ_KEY_DOWN;
      return TRUE;
    case CLUTTER_Print:
      *keyval = MOZ_KEY_PRINTSCREEN;
      return TRUE;
    case CLUTTER_Insert:
      *keyval = MOZ_KEY_INSERT;
      return TRUE;
    case CLUTTER_Delete:
      *keyval = MOZ_KEY_DELETE;
      return TRUE;
    case CLUTTER_semicolon:
      *keyval = MOZ_KEY_SEMICOLON;
      return TRUE;
    case CLUTTER_equal:
      *keyval = MOZ_KEY_EQUALS;
      return TRUE;
    case CLUTTER_Menu:
      *keyval = MOZ_KEY_CONTEXT_MENU;
      return TRUE;
    case CLUTTER_KP_0:
      *keyval = MOZ_KEY_NUMPAD0;
      return TRUE;
    case CLUTTER_KP_1:
      *keyval = MOZ_KEY_NUMPAD1;
      return TRUE;
    case CLUTTER_KP_2:
      *keyval = MOZ_KEY_NUMPAD2;
      return TRUE;
    case CLUTTER_KP_3:
      *keyval = MOZ_KEY_NUMPAD3;
      return TRUE;
    case CLUTTER_KP_4:
      *keyval = MOZ_KEY_NUMPAD4;
      return TRUE;
    case CLUTTER_KP_5:
      *keyval = MOZ_KEY_NUMPAD5;
      return TRUE;
    case CLUTTER_KP_6:
      *keyval = MOZ_KEY_NUMPAD6;
      return TRUE;
    case CLUTTER_KP_7:
      *keyval = MOZ_KEY_NUMPAD7;
      return TRUE;
    case CLUTTER_KP_8:
      *keyval = MOZ_KEY_NUMPAD8;
      return TRUE;
    case CLUTTER_KP_9:
      *keyval = MOZ_KEY_NUMPAD9;
      return TRUE;
    case CLUTTER_KP_Add:
      *keyval = MOZ_KEY_NUMPAD0;
      return TRUE;
    case CLUTTER_KP_Separator:
      *keyval = MOZ_KEY_SEPARATOR;
      return TRUE;
    case CLUTTER_KP_Subtract:
      *keyval = MOZ_KEY_SUBTRACT;
      return TRUE;
    case CLUTTER_KP_Decimal:
      *keyval = MOZ_KEY_DECIMAL;
      return TRUE;
    case CLUTTER_KP_Divide:
      *keyval = MOZ_KEY_DIVIDE;
      return TRUE;
    case CLUTTER_F1:
      *keyval = MOZ_KEY_F1;
      return TRUE;
    case CLUTTER_F2:
      *keyval = MOZ_KEY_F2;
      return TRUE;
    case CLUTTER_F3:
      *keyval = MOZ_KEY_F3;
      return TRUE;
    case CLUTTER_F4:
      *keyval = MOZ_KEY_F4;
      return TRUE;
    case CLUTTER_F5:
      *keyval = MOZ_KEY_F5;
      return TRUE;
    case CLUTTER_F6:
      *keyval = MOZ_KEY_F6;
      return TRUE;
    case CLUTTER_F7:
      *keyval = MOZ_KEY_F7;
      return TRUE;
    case CLUTTER_F8:
      *keyval = MOZ_KEY_F8;
      return TRUE;
    case CLUTTER_F9:
      *keyval = MOZ_KEY_F9;
      return TRUE;
    case CLUTTER_F10:
      *keyval = MOZ_KEY_F10;
      return TRUE;
    case CLUTTER_F11:
      *keyval = MOZ_KEY_F11;
      return TRUE;
    case CLUTTER_F12:
      *keyval = MOZ_KEY_F12;
      return TRUE;
    case CLUTTER_F13:
      *keyval = MOZ_KEY_F13;
      return TRUE;
    case CLUTTER_F14:
      *keyval = MOZ_KEY_F14;
      return TRUE;
    case CLUTTER_F15:
      *keyval = MOZ_KEY_F15;
      return TRUE;
    case CLUTTER_F16:
      *keyval = MOZ_KEY_F16;
      return TRUE;
    case CLUTTER_F17:
      *keyval = MOZ_KEY_F17;
      return TRUE;
    case CLUTTER_F18:
      *keyval = MOZ_KEY_F18;
      return TRUE;
    case CLUTTER_F19:
      *keyval = MOZ_KEY_F19;
      return TRUE;
    case CLUTTER_F20:
      *keyval = MOZ_KEY_F20;
      return TRUE;
    case CLUTTER_F21:
      *keyval = MOZ_KEY_F21;
      return TRUE;
    case CLUTTER_F22:
      *keyval = MOZ_KEY_F22;
      return TRUE;
    case CLUTTER_F23:
      *keyval = MOZ_KEY_F23;
      return TRUE;
    case CLUTTER_F24:
      *keyval = MOZ_KEY_F24;
      return TRUE;
    case CLUTTER_Num_Lock:
      *keyval = MOZ_KEY_NUM_LOCK;
      return TRUE;
    case CLUTTER_Scroll_Lock:
      *keyval = MOZ_KEY_SCROLL_LOCK;
      return TRUE;
    }
  

  if (g_ascii_isalnum (event->unicode_value))
    {
      *keyval = event->unicode_value;
      return TRUE;
    }

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
          priv->poll_source = g_idle_add ((GSourceFunc)poll_idle_cb,
                                          self);
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
  new_env = g_new (gchar *, env_size + 3);

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
    NULL, /* SHM name */
    NULL, /* Private mode */
    NULL
  };

  ClutterMozEmbed *self = CLUTTER_MOZEMBED (object);
  ClutterMozEmbedPrivate *priv = self->priv;
  GError *error = NULL;

  /* Set up out-of-process renderer */

  /* Generate names for pipe/shm, if not provided */
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

  if (!priv->shm_name)
    priv->shm_name = g_strdup_printf ("/mozheadless-%d-%d",
                                      getpid (), spawned_windows);

  spawned_windows++;

  /* Spawn renderer */
  argv[1] = priv->output_file;
  argv[2] = priv->input_file;
  argv[3] = priv->shm_name;
  if (priv->private)
    argv[4] = "p";

  if (priv->spawn)
    {
      if (g_getenv ("CLUTTER_MOZEMBED_DEBUG"))
        {
          g_message ("Waiting for '%s %s %s %s %s' to be run",
                     argv[0], argv[1], argv[2], argv[3],
                     priv->private ? argv[4] : "");
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
                                   PROP_SHM,
                                   g_param_spec_string ("shm",
                                                        "Named SHM",
                                                        "Named shared memory "
                                                        "region for image "
                                                        "buffer.",
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB |
                                                        G_PARAM_CONSTRUCT_ONLY));

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
                                                      0, G_MAXINT, 1000,
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
  priv->poll_timeout = 1000;
  priv->connect_timeout = 10000;
  priv->downloads = g_hash_table_new_full (g_direct_hash,
                                           g_direct_equal,
                                           NULL,
                                           _destroy_download_cb);
  priv->scrollbars = TRUE;

  clutter_actor_set_reactive (CLUTTER_ACTOR (self), TRUE);

  /* Turn off sync-size (we manually size the texture on allocate) */
  g_object_set (G_OBJECT (self), "sync-size", FALSE, NULL);

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
}

ClutterActor *
clutter_mozembed_new (void)
{
  return CLUTTER_ACTOR (g_object_new (CLUTTER_TYPE_MOZEMBED, NULL));
}

ClutterActor *
clutter_mozembed_new_with_parent (ClutterMozEmbed *parent)
{
  gchar *input, *output, *shm;
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
                "shm", &shm,
                NULL);
  CLUTTER_MOZEMBED (mozembed)->priv->private = parent->priv->private;

  clutter_mozembed_comms_send (parent->priv->output,
                               CME_COMMAND_NEW_WINDOW,
                               G_TYPE_STRING, input,
                               G_TYPE_STRING, output,
                               G_TYPE_STRING, shm,
                               G_TYPE_INVALID);

  g_free (input);
  g_free (output);
  g_free (shm);

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

gboolean
clutter_mozembed_get_private (ClutterMozEmbed *mozembed)
{
  return mozembed->priv->private;
}

