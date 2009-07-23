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

#include <config.h>

#ifdef SUPPORT_PLUGINS
#include <gtk/gtk.h>
#include <X11/Xlib.h>
#endif
#include <glib.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>

#include "clutter-mozheadless.h"
#include "clutter-mozembed-comms.h"
#include "clutter-mozheadless-history.h"
#include "clutter-mozheadless-prefs.h"
#include "clutter-mozheadless-downloads.h"
#include "clutter-mozheadless-cookies.h"
#include "clutter-mozheadless-certs.h"
#include "clutter-mozheadless-login-manager-storage.h"

#include "clutter-mozembed.h"

#include <nsAppDirectoryServiceDefs.h>

G_DEFINE_TYPE (ClutterMozHeadless, clutter_mozheadless, MOZ_TYPE_HEADLESS)

#define MOZHEADLESS_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), CLUTTER_TYPE_MOZHEADLESS, ClutterMozHeadlessPrivate))

enum
{
  PROP_0,

  PROP_INPUT,
  PROP_OUTPUT,
  PROP_SHM,
  PROP_CONNECT_TIMEOUT,
  PROP_PRIVATE
};

struct _ClutterMozHeadlessPrivate
{
  /* Connection/comms variables */
  GList           *views;
  gint             waiting_for_ack;
  gboolean         pending_resize;
  gchar           *input_file;
  gchar           *output_file;

  /* Shared memory variables */
  int              shm_fd;
  gchar           *shm_name;
  void            *mmap_start;
  size_t           mmap_length;

  /* Surface property variables */
  gint             surface_width;
  gint             surface_height;

  /* Synchronous call variables */
  ClutterMozEmbedCommand  sync_call;
  gchar                  *new_input_file;
  gchar                  *new_output_file;
  gchar                  *new_shm_name;

  /* Connection timeout variables */
  guint            connect_timeout;
  guint            connect_timeout_source;

  /* Page property variables */
  gboolean         private;
  guint            security;
};

static GMainLoop *mainloop;
static gint spawned_heads = 0;

static void block_until_command (ClutterMozHeadless     *moz_headless,
                                 ClutterMozEmbedCommand  command);

static gboolean input_io_func (GIOChannel              *source,
                               GIOCondition             condition,
                               ClutterMozHeadlessView  *view);

static void security_change_cb (ClutterMozHeadless *self,
                                void               *request,
                                guint               state,
                                gpointer            ignored);

void
send_feedback_all (ClutterMozHeadless      *headless,
                   ClutterMozEmbedFeedback  id,
                   ...)
{
  GList *v;
  ClutterMozHeadlessPrivate *priv = headless->priv;

  va_list args;

  for (v = priv->views; v; v = v->next)
    {
      ClutterMozHeadlessView *view = v->data;

      va_start (args, id);
      clutter_mozembed_comms_sendv (view->output, id, args);
      va_end (args);
    }
}

static void
location_cb (ClutterMozHeadless *headless)
{
  gchar *location;
  gboolean status, update;
  ClutterMozHeadlessPrivate *priv = headless->priv;

  location = moz_headless_get_location (MOZ_HEADLESS (headless));
  send_feedback_all (headless, CME_FEEDBACK_LOCATION,
                     G_TYPE_STRING, location,
                     G_TYPE_INVALID);

  status = update = (priv->security & CLUTTER_MOZEMBED_BAD_CERT) ? TRUE : FALSE;
  clutter_mozheadless_update_cert_status (location, &update);

  if (status != update)
    {
      if (update)
        priv->security += CLUTTER_MOZEMBED_BAD_CERT;
      else
        priv->security -= CLUTTER_MOZEMBED_BAD_CERT;
      security_change_cb (headless, NULL, priv->security, NULL);
    }

  g_free (location);
}

static void
title_cb (ClutterMozHeadless *headless)
{
  gchar *title;

  title = moz_headless_get_title (MOZ_HEADLESS (headless));
  send_feedback_all (headless, CME_FEEDBACK_TITLE,
                     G_TYPE_STRING, title,
                     G_TYPE_INVALID);

  g_free (title);
}

static void
icon_cb (ClutterMozHeadless *headless)
{
  gchar *icon;

  icon = moz_headless_get_icon (MOZ_HEADLESS (headless));
  send_feedback_all (headless, CME_FEEDBACK_TITLE,
                     G_TYPE_STRING, icon,
                     G_TYPE_INVALID);

  g_free (icon);
}

static void
progress_cb (ClutterMozHeadless *headless,
             gint                curprogress,
             gint                maxprogress)
{
  gdouble progress = curprogress / (gdouble)maxprogress;
  send_feedback_all (headless, CME_FEEDBACK_PROGRESS,
                     G_TYPE_DOUBLE, progress,
                     G_TYPE_INVALID);
}

static void
net_start_cb (ClutterMozHeadless *headless)
{
  send_feedback_all (headless, CME_FEEDBACK_NET_START, G_TYPE_INVALID);
}

static void
net_stop_cb (ClutterMozHeadless *headless)
{
  send_feedback_all (headless, CME_FEEDBACK_NET_STOP, G_TYPE_INVALID);
}

static gboolean
scroll_cb (MozHeadless *headless, MozHeadlessRect *rect, gint dx, gint dy)
{
  /*g_debug ("Scroll %d, %d", dx, dy);*/
  return FALSE;
}

static void
updated_cb (MozHeadless        *headless,
            gint                x,
            gint                y,
            gint                width,
            gint                height)
{
  gint doc_width, doc_height, sx, sy;
  GList *v;

  ClutterMozHeadlessPrivate *priv = CLUTTER_MOZHEADLESS (headless)->priv;

  /*g_debug ("Update +%d+%d %dx%d", x, y, width, height);*/

  msync (priv->mmap_start, priv->mmap_length, MS_SYNC);

  moz_headless_get_document_size (headless, &doc_width, &doc_height);
  moz_headless_get_scroll_pos (headless, &sx, &sy);

  /*g_debug ("Doc-size: %dx%d", doc_width, doc_height);*/
  send_feedback_all (CLUTTER_MOZHEADLESS (headless), CME_FEEDBACK_UPDATE,
                     G_TYPE_INT, x,
                     G_TYPE_INT, y,
                     G_TYPE_INT, width,
                     G_TYPE_INT, height,
                     G_TYPE_INT, priv->surface_width,
                     G_TYPE_INT, priv->surface_height,
                     G_TYPE_INT, sx,
                     G_TYPE_INT, sy,
                     G_TYPE_INT, doc_width,
                     G_TYPE_INT, doc_height,
                     G_TYPE_INVALID);

  for (v = priv->views; v; v = v->next)
    {
      ClutterMozHeadlessView *view = v->data;

      /* This shouldn't happen (because if we're waiting for an ack, we've
       * frozen updates), but put this check in just in case.
       */
      if (view->waiting_for_ack)
        continue;

      priv->waiting_for_ack ++;
      view->waiting_for_ack = TRUE;
    }

  moz_headless_freeze_updates (headless, TRUE);
}

static void
new_window_cb (MozHeadless *headless, MozHeadless **newEmbed, guint chromemask)
{
  ClutterMozHeadless *moz_headless = CLUTTER_MOZHEADLESS (headless);
  ClutterMozHeadlessPrivate *priv = moz_headless->priv;
  ClutterMozHeadlessView *view = (ClutterMozHeadlessView *)priv->views->data;

  clutter_mozembed_comms_send (view->output, CME_FEEDBACK_NEW_WINDOW,
                               G_TYPE_INT, chromemask,
                               G_TYPE_INVALID);
  block_until_command (moz_headless, CME_COMMAND_NEW_WINDOW_RESPONSE);

  if (priv->new_input_file && priv->new_output_file && priv->new_shm_name)
    {
      *newEmbed = g_object_new (CLUTTER_TYPE_MOZHEADLESS,
                                "chromeflags", chromemask,
                                "output", priv->new_output_file,
                                "input", priv->new_input_file,
                                "shm", priv->new_shm_name,
                                "private", priv->private,
                                NULL);
      moz_headless_set_chrome_mask (*newEmbed, chromemask);

      g_free (priv->new_input_file);
      g_free (priv->new_output_file);
      g_free (priv->new_shm_name);

      priv->new_input_file = NULL;
      priv->new_output_file = NULL;
      priv->new_shm_name = NULL;
    }
  else
    *newEmbed = NULL;
}

static void
destroy_browser_cb (ClutterMozHeadless *moz_headless)
{
  send_feedback_all (moz_headless, CME_FEEDBACK_CLOSED, G_TYPE_INVALID);
}

static void
link_message_cb (ClutterMozHeadless *self)
{
  gchar *message = moz_headless_get_link_message (MOZ_HEADLESS (self));
  send_feedback_all (self, CME_FEEDBACK_LINK_MESSAGE,
                     G_TYPE_STRING, message,
                     G_TYPE_INVALID);
  g_free (message);
}

static void
can_go_back_cb (ClutterMozHeadless *self, gboolean can_go_back)
{
  send_feedback_all (self, CME_FEEDBACK_CAN_GO_BACK,
                     G_TYPE_BOOLEAN, can_go_back,
                     G_TYPE_INVALID);
}

static void
can_go_forward_cb (ClutterMozHeadless *self, gboolean can_go_forward)
{
  send_feedback_all (self, CME_FEEDBACK_CAN_GO_FORWARD,
                     G_TYPE_BOOLEAN, can_go_forward,
                     G_TYPE_INVALID);
}

static void
size_to_cb (ClutterMozHeadless *self, gint width, gint height)
{
  ClutterMozHeadlessView *primary_view;

  ClutterMozHeadlessPrivate *priv = self->priv;

  if (!priv->views)
    return;

  primary_view = (ClutterMozHeadlessView *)priv->views->data;
  clutter_mozembed_comms_send (primary_view->output,
                               CME_FEEDBACK_SIZE_REQUEST,
                               G_TYPE_INT, width,
                               G_TYPE_INT, height,
                               G_TYPE_INVALID);
}

static void
security_change_cb (ClutterMozHeadless *self,
                    void               *request,
                    guint               state,
                    gpointer            ignored)
{
  ClutterMozHeadlessPrivate *priv = self->priv;

  priv->security = state | (priv->security & CLUTTER_MOZEMBED_BAD_CERT);
  send_feedback_all (self, CME_FEEDBACK_SECURITY,
                     G_TYPE_INT, priv->security,
                     G_TYPE_INVALID);
}

static void
show_tooltip_cb (ClutterMozHeadless *self,
                 const gchar        *text,
                 gint                x,
                 gint                y)
{
  send_feedback_all (self, CME_FEEDBACK_SHOW_TOOLTIP,
                     G_TYPE_INT, x,
                     G_TYPE_INT, y,
                     G_TYPE_STRING, text,
                     G_TYPE_INVALID);
}

static void
hide_tooltip_cb (ClutterMozHeadless *self)
{
  send_feedback_all (self, CME_FEEDBACK_HIDE_TOOLTIP, G_TYPE_INVALID);
}

static void
file_changed_cb (GFileMonitor           *monitor,
                 GFile                  *file,
                 GFile                  *other_file,
                 GFileMonitorEvent       event_type,
                 ClutterMozHeadlessView *view)
{
  gint fd;
  gint doc_width, doc_height, sx, sy;

  ClutterMozHeadlessPrivate *priv = view->parent->priv;

  if (event_type != G_FILE_MONITOR_EVENT_CREATED)
    return;

  g_signal_handlers_disconnect_by_func (monitor, file_changed_cb, view);
  g_file_monitor_cancel (monitor);
  g_object_unref (monitor);
  view->monitor = NULL;

  /* Opening input channel */
  fd = open (view->input_file, O_RDONLY | O_NONBLOCK);
  view->input = g_io_channel_unix_new (fd);
  g_io_channel_set_encoding (view->input, NULL, NULL);
  g_io_channel_set_buffered (view->input, FALSE);
  g_io_channel_set_close_on_unref (view->input, TRUE);
  view->watch_id = g_io_add_watch (view->input,
                                   G_IO_IN | G_IO_PRI | G_IO_ERR |
                                   G_IO_NVAL | G_IO_HUP,
                                   (GIOFunc)input_io_func,
                                   view);

  /* Send the shm name and an update to the new view */
  clutter_mozembed_comms_send (view->output, CME_FEEDBACK_SHM_NAME,
                               G_TYPE_STRING, priv->shm_name,
                               G_TYPE_INVALID);

  moz_headless_get_document_size (MOZ_HEADLESS (view->parent),
                                  &doc_width, &doc_height);
  moz_headless_get_scroll_pos (MOZ_HEADLESS (view->parent), &sx, &sy);

  /* If we have an active surface, send an update to this new view */
  if (priv->mmap_start)
    {
      clutter_mozembed_comms_send (view->output, CME_FEEDBACK_UPDATE,
                                   G_TYPE_INT, 0,
                                   G_TYPE_INT, 0,
                                   G_TYPE_INT, priv->surface_width,
                                   G_TYPE_INT, priv->surface_height,
                                   G_TYPE_INT, priv->surface_width,
                                   G_TYPE_INT, priv->surface_height,
                                   G_TYPE_INT, sx,
                                   G_TYPE_INT, sy,
                                   G_TYPE_INT, doc_width,
                                   G_TYPE_INT, doc_height,
                                   G_TYPE_INVALID);

      if (!priv->waiting_for_ack)
        moz_headless_freeze_updates (MOZ_HEADLESS (view->parent), TRUE);

      view->waiting_for_ack = TRUE;
      priv->waiting_for_ack ++;
    }

  /* Inform if we're private */
  /* FIXME: I don't think we need to do this */
  if (priv->private)
    clutter_mozembed_comms_send (view->output, CME_FEEDBACK_PRIVATE,
                                 G_TYPE_BOOLEAN, TRUE,
                                 G_TYPE_INVALID);
}

static void
cursor_changed_cb (MozHeadlessCursorType    type,
                   const MozHeadlessCursor *special,
                   gpointer                 data)
{
  ClutterMozHeadless *mozheadless = data;
  /* TODO - support special cursors */
  send_feedback_all (mozheadless, CME_FEEDBACK_CURSOR,
                     G_TYPE_INT, type,
                     G_TYPE_INVALID);
}

static void
clutter_mozheadless_create_view (ClutterMozHeadless *self,
                                 gchar              *input_file,
                                 gchar              *output_file)
{
  GFile *file;
  gint fd;

  ClutterMozHeadlessPrivate *priv = self->priv;
  ClutterMozHeadlessView *view = g_new0 (ClutterMozHeadlessView, 1);

  priv->views = g_list_append (priv->views, view);

  view->parent = self;
  /* This takes ownership of the input_file and output_file strings */
  view->input_file = input_file;
  view->output_file = output_file;

  mkfifo (view->output_file, S_IWUSR | S_IRUSR);
  fd = open (view->output_file, O_RDWR | O_NONBLOCK);
  view->output = g_io_channel_unix_new (fd);
  g_io_channel_set_encoding (view->output, NULL, NULL);
  g_io_channel_set_buffered (view->output, FALSE);
  g_io_channel_set_close_on_unref (view->output, TRUE);

  file = g_file_new_for_path (view->input_file);
  view->monitor = g_file_monitor_file (file, 0, NULL, NULL);
  g_object_unref (file);
  g_signal_connect (view->monitor, "changed",
                    G_CALLBACK (file_changed_cb), view);

  if (g_file_test (view->input_file, G_FILE_TEST_EXISTS))
    file_changed_cb (view->monitor, NULL, NULL,
                     G_FILE_MONITOR_EVENT_CREATED, view);
}

static gboolean
send_mack (ClutterMozHeadlessView *view)
{
  view->mack_source = 0;
  clutter_mozembed_comms_send (view->output,
                               CME_FEEDBACK_MOTION_ACK,
                               G_TYPE_INVALID);
  return FALSE;
}

static gboolean
send_sack_cb (ClutterMozHeadlessView *view)
{
  view->sack_source = 0;
  clutter_mozembed_comms_send (view->output,
                               CME_FEEDBACK_SCROLL_ACK,
                               G_TYPE_INVALID);
  return FALSE;
}

static void
send_sack (ClutterMozHeadlessView *view)
{
  if (!view->sack_source)
    view->sack_source =
      g_idle_add_full (G_PRIORITY_LOW, (GSourceFunc)send_sack_cb, view, NULL);
}

static void
clutter_moz_headless_resize (ClutterMozHeadless *moz_headless)
{
  MozHeadless *headless = MOZ_HEADLESS (moz_headless);
  ClutterMozHeadlessPrivate *priv = moz_headless->priv;

  moz_headless_set_surface (headless, NULL, 0, 0, 0);
  moz_headless_set_size (headless,
                         priv->surface_width,
                         priv->surface_height);

  if (priv->mmap_start)
    munmap (priv->mmap_start, priv->mmap_length);

  priv->mmap_length = priv->surface_width * priv->surface_height * 4;
  ftruncate (priv->shm_fd, priv->mmap_length);
  priv->mmap_start = mmap (NULL, priv->mmap_length, PROT_READ | PROT_WRITE,
                           MAP_SHARED, priv->shm_fd, 0);

  moz_headless_set_surface (headless, priv->mmap_start,
                            priv->surface_width, priv->surface_height,
                            priv->surface_width * 4);
}

static void
clutter_moz_headless_unfreeze (ClutterMozHeadless *moz_headless)
{
  MozHeadless *headless = MOZ_HEADLESS (moz_headless);
  ClutterMozHeadlessPrivate *priv = moz_headless->priv;

  moz_headless_freeze_updates (headless, FALSE);

  if (priv->pending_resize)
    {
      clutter_moz_headless_resize (moz_headless);
      priv->pending_resize = FALSE;
    }
}

static void
process_command (ClutterMozHeadlessView *view, ClutterMozEmbedCommand command)
{
  ClutterMozHeadless *moz_headless = view->parent;
  ClutterMozHeadlessPrivate *priv = moz_headless->priv;
  MozHeadless *headless = MOZ_HEADLESS (moz_headless);

  /*g_debug ("Processing command: %d", command);*/

  if (priv->sync_call && (priv->sync_call == command))
    priv->sync_call = 0;

  switch (command)
    {
      case CME_COMMAND_UPDATE_ACK :
        {
          view->waiting_for_ack = FALSE;
          priv->waiting_for_ack --;

          if (priv->waiting_for_ack == 0)
            clutter_moz_headless_unfreeze (moz_headless);

          break;
        }
      case CME_COMMAND_OPEN_URL :
        {
          gchar *url = clutter_mozembed_comms_receive_string (view->input);
          moz_headless_load_url (headless, url);
          g_free (url);
          break;
        }
      case CME_COMMAND_RESIZE :
        {
          clutter_mozembed_comms_receive (view->input,
                                          G_TYPE_INT, &priv->surface_width,
                                          G_TYPE_INT, &priv->surface_height,
                                          G_TYPE_INVALID);

          if (priv->waiting_for_ack)
            priv->pending_resize = TRUE;
          else
            clutter_moz_headless_resize (moz_headless);

          break;
        }
      case CME_COMMAND_MOTION :
        {
          gint x, y;
          MozHeadlessModifier m;

          clutter_mozembed_comms_receive (view->input,
                                          G_TYPE_INT, &x,
                                          G_TYPE_INT, &y,
                                          G_TYPE_INT, &m,
                                          G_TYPE_INVALID);

          moz_headless_motion (headless, x, y, m);

          /* This is done so that we definitely get to do any redrawing before we
           * send an acknowledgement.
           */
          if (!view->mack_source)
            view->mack_source =
              g_idle_add_full (G_PRIORITY_LOW,
                               (GSourceFunc)send_mack,
                               view,
                               NULL);
          else
            g_warning ("Received a motion event before "
                       "sending acknowledgement");

          break;
        }
      case CME_COMMAND_BUTTON_PRESS :
        {
          gint x, y, button, count;
          MozHeadlessModifier m;

          clutter_mozembed_comms_receive (view->input,
                                          G_TYPE_INT, &x,
                                          G_TYPE_INT, &y,
                                          G_TYPE_INT, &button,
                                          G_TYPE_INT, &count,
                                          G_TYPE_INT, &m,
                                          G_TYPE_INVALID);

          moz_headless_button_press (headless, x, y, button, count, m);

          break;
        }
      case CME_COMMAND_BUTTON_RELEASE :
        {
          gint x, y, button;
          MozHeadlessModifier m;

          clutter_mozembed_comms_receive (view->input,
                                          G_TYPE_INT, &x,
                                          G_TYPE_INT, &y,
                                          G_TYPE_INT, &button,
                                          G_TYPE_INT, &m,
                                          G_TYPE_INVALID);

          moz_headless_button_release (headless, x, y, button, m);

          break;
        }
      case CME_COMMAND_KEY_PRESS :
        {
          MozHeadlessKey key;
          gunichar unicode_char;
          MozHeadlessModifier m;

          clutter_mozembed_comms_receive (view->input,
                                          G_TYPE_INT, &key,
                                          G_TYPE_INT, &unicode_char,
                                          G_TYPE_INT, &m,
                                          G_TYPE_INVALID);

          moz_headless_key_press (headless, key, unicode_char, m);

          break;
        }
      case CME_COMMAND_KEY_RELEASE :
        {
          MozHeadlessKey key;
          MozHeadlessModifier m;

          clutter_mozembed_comms_receive (view->input,
                                          G_TYPE_INT, &key,
                                          G_TYPE_INT, &m,
                                          G_TYPE_INVALID);

          moz_headless_key_release (headless, key, m);

          break;
        }
      case CME_COMMAND_SCROLL :
        {
          gint dx, dy;

          clutter_mozembed_comms_receive (view->input,
                                          G_TYPE_INT, &dx,
                                          G_TYPE_INT, &dy,
                                          G_TYPE_INVALID);

          moz_headless_scroll (headless, dx, dy);

          send_sack (view);

          break;
        }
      case CME_COMMAND_SCROLL_TO :
        {
          gint x, y;

          clutter_mozembed_comms_receive (view->input,
                                          G_TYPE_INT, &x,
                                          G_TYPE_INT, &y,
                                          G_TYPE_INVALID);

          moz_headless_set_scroll_pos (headless, x, y);

          send_sack (view);

          break;
        }
      case CME_COMMAND_GET_CAN_GO_BACK :
        {
          clutter_mozembed_comms_send (view->output,
                                       CME_FEEDBACK_CAN_GO_BACK,
                                       G_TYPE_BOOLEAN,
                                       moz_headless_can_go_back (headless),
                                       G_TYPE_INVALID);
          break;
        }
      case CME_COMMAND_GET_CAN_GO_FORWARD :
        {
          clutter_mozembed_comms_send (view->output,
                                       CME_FEEDBACK_CAN_GO_FORWARD,
                                       G_TYPE_BOOLEAN,
                                       moz_headless_can_go_back (headless),
                                       G_TYPE_INVALID);
          break;
        }
      case CME_COMMAND_GET_SHM_NAME :
        {
          clutter_mozembed_comms_send (view->output,
                                       CME_FEEDBACK_SHM_NAME,
                                       G_TYPE_STRING, priv->shm_name,
                                       G_TYPE_INVALID);
          break;
        }
      case CME_COMMAND_BACK :
        {
          moz_headless_go_back (headless);
          break;
        }
      case CME_COMMAND_FORWARD :
        {
          moz_headless_go_forward (headless);
          break;
        }
      case CME_COMMAND_STOP :
        {
          moz_headless_stop_load (headless);
          break;
        }
      case CME_COMMAND_REFRESH :
        {
          moz_headless_reload (headless, MOZ_HEADLESS_FLAG_RELOADNORMAL);
          break;
        }
      case CME_COMMAND_RELOAD :
        {
          moz_headless_reload (headless, MOZ_HEADLESS_FLAG_RELOADBYPASSCACHE);
          break;
        }
      case CME_COMMAND_SET_CHROME :
        {
          gint chrome = clutter_mozembed_comms_receive_int (view->input);
          moz_headless_set_chrome_mask (headless, chrome);
          break;
        }
      case CME_COMMAND_TOGGLE_CHROME :
        {
          guint32 chrome = moz_headless_get_chrome_mask (headless);
          chrome ^= clutter_mozembed_comms_receive_int (view->input);
          moz_headless_set_chrome_mask (headless, chrome);
          break;
        }
      case CME_COMMAND_QUIT :
        {
          /* FIXME: Er, will this unref not cause a crash? Must check this... */
          send_feedback_all (moz_headless, CME_FEEDBACK_CLOSED, G_TYPE_INVALID);
          g_object_unref (moz_headless);
          break;
        }
      case CME_COMMAND_NEW_VIEW :
        {
          gchar *input, *output;

          clutter_mozembed_comms_receive (view->input,
                                          G_TYPE_STRING, &input,
                                          G_TYPE_STRING, &output,
                                          G_TYPE_INVALID);

          /* create_view takes ownership of the input and output strings */
          clutter_mozheadless_create_view (moz_headless, input, output);

          break;
        }
      case CME_COMMAND_NEW_WINDOW :
        {
          gchar *input, *output, *shm;

          clutter_mozembed_comms_receive (view->input,
                                          G_TYPE_STRING, &input,
                                          G_TYPE_STRING, &output,
                                          G_TYPE_STRING, &shm,
                                          G_TYPE_INVALID);

          g_object_new (CLUTTER_TYPE_MOZHEADLESS,
                        "input", input,
                        "output", output,
                        "shm", shm,
                        "private", priv->private,
                        NULL);

          break;
        }
      case CME_COMMAND_NEW_WINDOW_RESPONSE :
        {
          if (clutter_mozembed_comms_receive_boolean (view->input))
            {
              clutter_mozembed_comms_receive (view->input,
                                              G_TYPE_STRING, &priv->new_input_file,
                                              G_TYPE_STRING, &priv->new_output_file,
                                              G_TYPE_STRING, &priv->new_shm_name,
                                              G_TYPE_INVALID);
            }
          else
            {
              g_free (priv->new_input_file);
              priv->new_input_file = NULL;
              g_free (priv->new_output_file);
              priv->new_output_file = NULL;
              g_free (priv->new_shm_name);
              priv->new_shm_name = NULL;
            }
          break;
        }
      case CME_COMMAND_FOCUS :
        {
          gboolean focus = clutter_mozembed_comms_receive_boolean (view->input);
          moz_headless_focus (MOZ_HEADLESS (moz_headless), focus);
          break;
        }
      case CME_COMMAND_PLUGIN_WINDOW :
        {
#ifdef SUPPORT_PLUGINS
          Window viewport_window = (Window)
            clutter_mozembed_comms_receive_ulong (view->input);
          moz_headless_set_plugin_window (MOZ_HEADLESS (moz_headless),
                                          viewport_window);
#endif
          break;
        }
    case CME_COMMAND_PURGE_SESSION_HISTORY :
        {
          moz_headless_purge_session_history (MOZ_HEADLESS (moz_headless));
          break;
        }
      default :
        g_warning ("Unknown command (%d)", command);
    }
}

static void
disconnect_view (ClutterMozHeadlessView *view)
{
  ClutterMozHeadlessPrivate *priv = view->parent->priv;

  if (view->waiting_for_ack)
    {
      priv->waiting_for_ack --;
      if (priv->waiting_for_ack == 0)
        clutter_moz_headless_unfreeze (view->parent);
    }

  if (view->monitor)
    {
      g_file_monitor_cancel (view->monitor);
      g_object_unref (view->monitor);
      view->monitor = NULL;
    }

  if (view->watch_id)
    {
      g_source_remove (view->watch_id);
      view->watch_id = 0;
    }

  if (view->mack_source)
    {
      g_source_remove (view->mack_source);
      view->mack_source = 0;
    }

  if (view->sack_source)
    {
      g_source_remove (view->sack_source);
      view->sack_source = 0;
    }

  if (view->input)
    {
      GError *error = NULL;

      if (g_io_channel_shutdown (view->input, FALSE, &error) ==
          G_IO_STATUS_ERROR)
        {
          g_warning ("Error closing input channel: %s", error->message);
          g_error_free (error);
        }

      g_io_channel_unref (view->input);
      view->input = NULL;
    }
  g_remove (view->input_file);

  if (view->output)
    {
      GError *error = NULL;

      if (g_io_channel_shutdown (view->output, FALSE, &error) ==
          G_IO_STATUS_ERROR)
        {
          g_warning ("Error closing output channel: %s", error->message);
          g_error_free (error);
        }

      g_io_channel_unref (view->output);
      view->output = NULL;
    }
  g_remove (view->output_file);

  g_free (view->output_file);
  g_free (view->input_file);
  g_free (view);

  priv->views = g_list_remove (priv->views, view);
}

static gboolean
input_io_func (GIOChannel              *source,
               GIOCondition             condition,
               ClutterMozHeadlessView  *view)
{
  /* FYI: Maximum URL length in IE is 2083 characters */
  ClutterMozEmbedCommand command;
  gsize length;
  GError *error = NULL;
  gboolean result = TRUE;

  ClutterMozHeadless *moz_headless = view->parent;
  ClutterMozHeadlessPrivate *priv = moz_headless->priv;

  while (condition & (G_IO_PRI | G_IO_IN))
    {
      GIOStatus status;

      /* We've received a connection, remove the disconnect timeout */
      if (priv->connect_timeout_source)
        {
          g_source_remove (priv->connect_timeout_source);
          priv->connect_timeout_source = 0;
        }

      status = g_io_channel_read_chars (source,
                                        (gchar *)(&command),
                                        sizeof (command),
                                        &length,
                                        &error);
      if (status == G_IO_STATUS_NORMAL)
        {
          process_command (view, command);
        }
      else if (status == G_IO_STATUS_ERROR)
        {
          g_warning ("Error reading from source: %s", error->message);
          g_error_free (error);
          result = FALSE;
        }
      else if (status == G_IO_STATUS_EOF)
        {
          g_warning ("End of file");
          result = FALSE;
        }

      condition = g_io_channel_get_buffer_condition (source);
    }

  if (condition & G_IO_HUP)
    {
      /* Don't warn on this, this is fine */
      /*g_warning ("Hung up");*/
      result = FALSE;
    }

  if (condition & G_IO_ERR)
    {
      g_warning ("Error");
      result = FALSE;
    }

  if (condition & G_IO_NVAL)
    {
      g_warning ("Invalid request");
      result = FALSE;
    }

  if (condition & ~(G_IO_PRI | G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL))
    {
      g_warning ("Unexpected IO condition");
      result = FALSE;
    }

  if (!result)
    {
      /* Kill this head or disconnect the view */
      if ((priv->views) && (view == priv->views->data))
        g_object_unref (moz_headless);
      else
        disconnect_view (view);
    }

  return result;
}

static void
block_until_command (ClutterMozHeadless     *moz_headless,
                     ClutterMozEmbedCommand  command)
{
  ClutterMozHeadlessPrivate *priv = moz_headless->priv;
  ClutterMozHeadlessView *view = priv->views->data;

  priv->sync_call = command;

  /* FIXME: There needs to be a time limit here, or we can hang if the front-end
   *        hangs. Here or in input_io_func anyway...
   */
  while (input_io_func (view->input, G_IO_IN, view) && priv->sync_call);

  if (priv->sync_call)
    g_warning ("Error making synchronous call to backend");
}

static void
clutter_mozheadless_get_property (GObject *object, guint property_id,
                                  GValue *value, GParamSpec *pspec)
{
  ClutterMozHeadlessPrivate *priv = CLUTTER_MOZHEADLESS (object)->priv;
  
  switch (property_id) {
  case PROP_INPUT :
    g_value_set_string (value, priv->input_file);
    break;

  case PROP_OUTPUT :
    g_value_set_string (value, priv->output_file);
    break;

  case PROP_SHM :
    g_value_set_string (value, priv->shm_name);
    break;

  case PROP_CONNECT_TIMEOUT :
    g_value_set_uint (value, priv->connect_timeout);
    break;

  case PROP_PRIVATE :
    g_value_set_boolean (value, priv->private);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
clutter_mozheadless_set_property (GObject *object, guint property_id,
                                  const GValue *value, GParamSpec *pspec)
{
  ClutterMozHeadlessPrivate *priv = CLUTTER_MOZHEADLESS (object)->priv;

  switch (property_id) {
  case PROP_INPUT :
    priv->input_file = g_value_dup_string (value);
    break;

  case PROP_OUTPUT :
    priv->output_file = g_value_dup_string (value);
    break;

  case PROP_SHM :
    priv->shm_name = g_value_dup_string (value);
    break;

  case PROP_CONNECT_TIMEOUT :
    priv->connect_timeout = g_value_get_uint (value);
    break;

  case PROP_PRIVATE :
    priv->private = g_value_get_boolean (value);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
clutter_mozheadless_dispose (GObject *object)
{
  ClutterMozHeadlessPrivate *priv = CLUTTER_MOZHEADLESS (object)->priv;

  if (priv->connect_timeout_source)
    {
      g_source_remove (priv->connect_timeout_source);
      priv->connect_timeout_source = 0;
    }

  while (priv->views)
    {
      ClutterMozHeadlessView *view = priv->views->data;
      disconnect_view (view);
    }

  G_OBJECT_CLASS (clutter_mozheadless_parent_class)->dispose (object);
}

static void
clutter_mozheadless_finalize (GObject *object)
{
  ClutterMozHeadlessPrivate *priv = CLUTTER_MOZHEADLESS (object)->priv;
  
  shm_unlink (priv->shm_name);

  g_free (priv->shm_name);
  
  spawned_heads --;
  if (spawned_heads <= 0)
    g_main_loop_quit (mainloop);
  
  G_OBJECT_CLASS (clutter_mozheadless_parent_class)->finalize (object);
}

static gboolean
connect_timeout_cb (ClutterMozHeadless *self)
{
  g_object_unref (G_OBJECT (self));
  return FALSE;
}

static void
clutter_mozheadless_constructed (GObject *object)
{
  ClutterMozHeadless *self = CLUTTER_MOZHEADLESS (object);
  ClutterMozHeadlessPrivate *priv = self->priv;

  if (G_OBJECT_CLASS (clutter_mozheadless_parent_class)->constructed)
    G_OBJECT_CLASS (clutter_mozheadless_parent_class)->constructed (object);

  /* create_view takes ownership of the priv->input_file and
     priv->output_file strings */
  clutter_mozheadless_create_view (self, priv->input_file, priv->output_file);

  if (!priv->shm_name)
    {
      priv->shm_name = g_strdup_printf ("/mozheadless-%d-%d",
                                        getpid (),
                                        spawned_heads);
    }
  priv->shm_fd = shm_open (priv->shm_name, O_CREAT | O_RDWR | O_TRUNC, 0666);
  if (priv->shm_fd == -1)
    g_error ("Error opening shared memory");

  g_signal_connect (object, "location",
                    G_CALLBACK (location_cb), NULL);
  g_signal_connect (object, "title",
                    G_CALLBACK (title_cb), NULL);
  g_signal_connect (object, "progress",
                    G_CALLBACK (progress_cb), NULL);
  g_signal_connect (object, "icon",
                    G_CALLBACK (icon_cb), NULL);
  g_signal_connect (object, "net-start",
                    G_CALLBACK (net_start_cb), NULL);
  g_signal_connect (object, "net-stop",
                    G_CALLBACK (net_stop_cb), NULL);
  g_signal_connect (object, "scroll",
                    G_CALLBACK (scroll_cb), NULL);
  g_signal_connect (object, "updated",
                    G_CALLBACK (updated_cb), NULL);
  g_signal_connect (object, "new-window",
                    G_CALLBACK (new_window_cb), NULL);
  g_signal_connect (object, "destroy-browser",
                    G_CALLBACK (destroy_browser_cb), NULL);
  g_signal_connect (object, "link-message",
                    G_CALLBACK (link_message_cb), NULL);
  g_signal_connect (object, "can-go-back",
                    G_CALLBACK (can_go_back_cb), NULL);
  g_signal_connect (object, "can-go-forward",
                    G_CALLBACK (can_go_forward_cb), NULL);
  g_signal_connect (object, "size-to",
                    G_CALLBACK (size_to_cb), NULL);
  g_signal_connect (object, "security_change",
                    G_CALLBACK (security_change_cb), NULL);
  g_signal_connect (object, "show-tooltip",
                    G_CALLBACK (show_tooltip_cb), NULL);
  g_signal_connect (object, "hide-tooltip",
                    G_CALLBACK (hide_tooltip_cb), NULL);

  spawned_heads ++;

  if (priv->connect_timeout)
    priv->connect_timeout_source =
      g_timeout_add (priv->connect_timeout,
                     (GSourceFunc)connect_timeout_cb,
                     object);
}

static void
clutter_mozheadless_class_init (ClutterMozHeadlessClass *klass)
{
  GObjectClass      *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (ClutterMozHeadlessPrivate));

  object_class->get_property = clutter_mozheadless_get_property;
  object_class->set_property = clutter_mozheadless_set_property;
  object_class->dispose = clutter_mozheadless_dispose;
  object_class->finalize = clutter_mozheadless_finalize;
  object_class->constructed = clutter_mozheadless_constructed;

  g_object_class_install_property (object_class,
                                   PROP_INPUT,
                                   g_param_spec_string ("input",
                                                        "Input pipe file",
                                                        "Communications pipe "
                                                        "file name, for input.",
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB |
                                                        G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (object_class,
                                   PROP_OUTPUT,
                                   g_param_spec_string ("output",
                                                        "Output pipe file",
                                                        "Communications pipe "
                                                        "file name, for output.",
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
                                   PROP_PRIVATE,
                                   g_param_spec_boolean ("private",
                                                         "Private",
                                                         "Private mode.",
                                                         FALSE,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_NICK |
                                                         G_PARAM_STATIC_BLURB |
                                                         G_PARAM_CONSTRUCT_ONLY));
}

static void
clutter_mozheadless_init (ClutterMozHeadless *self)
{
  ClutterMozHeadlessPrivate *priv = self->priv = MOZHEADLESS_PRIVATE (self);
  priv->connect_timeout = 10000;
}

ClutterMozHeadless *
clutter_mozheadless_new (void)
{
  return g_object_new (CLUTTER_TYPE_MOZHEADLESS, NULL);
}

#ifdef BREAK_ON_EXIT
static void
atexit_func ()
{
  G_BREAKPOINT ();
}
#endif

int
main (int argc, char **argv)
{
  ClutterMozHeadless *moz_headless;
  const gchar *paths;
  gboolean private;

#ifdef SUPPORT_PLUGINS
  gtk_init (&argc, &argv);
#endif

 if ((argc != 4) && (argc != 5))
    {
      printf ("Usage: %s <output pipe> <input pipe> <shm name>\n", argv[0]);
      return 1;
    }

#ifdef BREAK_ON_EXIT
  atexit (atexit_func);
#endif

  g_type_init ();

  /* Initialise mozilla */
  moz_headless_set_path (MOZHOME);
  moz_headless_set_comp_path (PKGDATADIR);
  moz_headless_set_directory (NS_APP_USER_MIMETYPES_50_FILE,
                              PKGDATADIR "/mimeTypes.rdf");

  if ((paths = g_getenv ("CLUTTER_MOZEMBED_COMP_PATHS")))
    {
      gchar **pathsv = g_strsplit (paths, ":", -1), **p;
      for (p = pathsv; *p; p++)
        moz_headless_add_comp_path (*p);
      g_strfreev (pathsv);
    }

  if ((paths = g_getenv ("CLUTTER_MOZEMBED_CHROME_PATHS")))
    {
      gchar **pathsv = g_strsplit (paths, ":", -1), **p;
      for (p = pathsv; *p; p++)
        moz_headless_add_chrome_path (*p);
      g_strfreev (pathsv);
    }

  moz_headless_push_startup ();

  private = (argc > 4) ? (*argv[4] == 'p') : FALSE;

  clutter_mozheadless_prefs_init ();
  clutter_mozheadless_downloads_init ();
  clutter_mozheadless_certs_init ();
  if (!private)
    {
      clutter_mozheadless_history_init ();
      clutter_mozheadless_cookies_init ();
      clutter_mozheadless_login_manager_storage_init ();
    }

  moz_headless = g_object_new (CLUTTER_TYPE_MOZHEADLESS,
                               "output", argv[1],
                               "input", argv[2],
                               "shm", argv[3],
                               "private", private,
                               NULL);

  moz_headless_set_change_cursor_callback (cursor_changed_cb,
                                           moz_headless);

  /* Begin */
  mainloop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (mainloop);

  moz_headless_pop_startup ();
  clutter_mozheadless_prefs_deinit ();
  clutter_mozheadless_downloads_deinit ();
  clutter_mozheadless_certs_deinit ();
  if (!private)
    {
      clutter_mozheadless_history_deinit ();
      clutter_mozheadless_cookies_deinit ();
      clutter_mozheadless_login_manager_storage_deinit ();
    }

  return 0;
}
