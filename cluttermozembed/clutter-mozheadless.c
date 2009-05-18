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
#include <gio/gio.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>

#include "clutter-mozheadless.h"
#include "clutter-mozheadless-history.h"
#include "clutter-mozheadless-prefs.h"

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
  PROP_CONNECT_TIMEOUT
};

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
} ClutterMozHeadlessView;

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
  const gchar     *sync_call;
  gchar           *new_input_file;
  gchar           *new_output_file;
  gchar           *new_shm_name;

  /* Connection timeout variables */
  guint            connect_timeout;
  guint            connect_timeout_source;
};

static GMainLoop *mainloop;
static gint spawned_heads = 0;

static void block_until_command (ClutterMozHeadless *moz_headless,
                                 const gchar        *command);

static gboolean input_io_func (GIOChannel              *source,
                               GIOCondition             condition,
                               ClutterMozHeadlessView  *view);


static void
send_feedback (ClutterMozHeadlessView *view,
               const gchar            *feedback)
{
  /*g_debug ("Sending feedback '%s' to view %p", feedback, view);*/
  g_io_channel_write_chars (view->output, feedback,
                            strlen (feedback) + 1, NULL, NULL);
  g_io_channel_flush (view->output, NULL);
}

static void
send_feedback_all (ClutterMozHeadless     *headless,
                   const gchar            *feedback)
{
  GList *v;
  ClutterMozHeadlessPrivate *priv = headless->priv;

  for (v = priv->views; v; v = v->next)
    {
      ClutterMozHeadlessView *view = v->data;
      send_feedback (view, feedback);
    }
}

static void
location_cb (ClutterMozHeadless *headless)
{
  gchar *location, *feedback;
  
  location = moz_headless_get_location (MOZ_HEADLESS (headless));
  feedback = g_strdup_printf ("location %s", location);
  
  send_feedback_all (headless, feedback);
  
  g_free (feedback);
  g_free (location);
}

static void
title_cb (ClutterMozHeadless *headless)
{
  gchar *title, *feedback;
  
  title = moz_headless_get_title (MOZ_HEADLESS (headless));
  feedback = g_strdup_printf ("title %s", title);
  
  send_feedback_all (headless, feedback);
  
  g_free (feedback);
  g_free (title);
}

static void
icon_cb (ClutterMozHeadless *headless)
{
  gchar *icon, *feedback;

  icon = moz_headless_get_icon (MOZ_HEADLESS (headless));
  feedback = g_strdup_printf ("icon %s", icon);

  send_feedback_all (headless, feedback);

  g_free (feedback);
  g_free (icon);
}

static void
progress_cb (ClutterMozHeadless *headless,
             gint                curprogress,
             gint                maxprogress)
{
  gchar *feedback;
  gdouble progress;
  
  progress = curprogress / (gdouble)maxprogress;
  feedback = g_strdup_printf ("progress %lf", progress);
  
  send_feedback_all (headless, feedback);
  
  g_free (feedback);
}

static void
net_start_cb (ClutterMozHeadless *headless)
{
  send_feedback_all (headless, "net-start");
}

static void
net_stop_cb (ClutterMozHeadless *headless)
{
  send_feedback_all (headless, "net-stop");
}

static gboolean
scroll_cb (MozHeadless *headless, MozHeadlessRect *rect, gint dx, gint dy)
{
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
  gchar *feedback;
  
  ClutterMozHeadlessPrivate *priv = CLUTTER_MOZHEADLESS (headless)->priv;
  
  /*g_debug ("Update +%d+%d %dx%d", x, y, width, height);*/

  msync (priv->mmap_start, priv->mmap_length, MS_SYNC);

  moz_headless_get_document_size (headless, &doc_width, &doc_height);
  moz_headless_get_scroll_pos (headless, &sx, &sy);

  /*g_debug ("Doc-size: %dx%d", doc_width, doc_height);*/
  
  feedback = g_strdup_printf ("update %d %d %d %d %d %d %d %d %d %d",
                              x, y, width, height,
                              priv->surface_width, priv->surface_height,
                              sx, sy, doc_width, doc_height);
  send_feedback_all (CLUTTER_MOZHEADLESS (headless), feedback);
  g_free (feedback);
  
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
  gchar *feedback;

  ClutterMozHeadless *moz_headless = CLUTTER_MOZHEADLESS (headless);
  ClutterMozHeadlessPrivate *priv = moz_headless->priv;

  feedback = g_strdup_printf ("new-window? %d", chromemask);
  send_feedback ((ClutterMozHeadlessView *)priv->views->data, feedback);
  g_free (feedback);
  block_until_command (moz_headless, "new-window-response");

  if (priv->new_input_file && priv->new_output_file && priv->new_shm_name)
    {
      *newEmbed = g_object_new (CLUTTER_TYPE_MOZHEADLESS,
                                "output", priv->new_output_file,
                                "input", priv->new_input_file,
                                "shm", priv->new_shm_name,
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
  send_feedback_all (moz_headless, "closed");
}

static void
link_message_cb (ClutterMozHeadless *self)
{
  gchar *feedback, *message;

  message = moz_headless_get_link_message (MOZ_HEADLESS (self));
  feedback = g_strconcat ("link ", message, NULL);
  g_free (message);

  send_feedback_all (self, feedback);

  g_free (feedback);
}

static void
can_go_back_cb (ClutterMozHeadless *self, gboolean can_go_back)
{
  gchar *feedback =
    g_strdup_printf ("back %d", can_go_back);
  send_feedback_all (self, feedback);
  g_free (feedback);
}

static void
can_go_forward_cb (ClutterMozHeadless *self, gboolean can_go_forward)
{
  gchar *feedback =
    g_strdup_printf ("forward %d", can_go_forward);
  send_feedback_all (self, feedback);
  g_free (feedback);
}

static void
size_to_cb (ClutterMozHeadless *self, gint width, gint height)
{
  gchar *feedback;
  ClutterMozHeadlessView *primary_view;

  ClutterMozHeadlessPrivate *priv = self->priv;

  if (!priv->views)
    return;

  primary_view = (ClutterMozHeadlessView *)priv->views->data;
  feedback = g_strdup_printf ("size-request %d %d", width, height);
  send_feedback (primary_view, feedback);
  g_free (feedback);
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
  gchar *feedback;
  
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
  g_io_add_watch (view->input,
                  G_IO_IN | G_IO_ERR | G_IO_NVAL | G_IO_HUP,
                  (GIOFunc)input_io_func,
                  view);

  /* Send the shm name and an update to the new view */
  feedback = g_strdup_printf ("shm-name %s", priv->shm_name);
  send_feedback (view, feedback);
  g_free (feedback);

  moz_headless_get_document_size (MOZ_HEADLESS (view->parent),
                                  &doc_width, &doc_height);
  moz_headless_get_scroll_pos (MOZ_HEADLESS (view->parent), &sx, &sy);

  /* If we have an active surface, send an update to this new view */
  if (priv->mmap_start)
    {
      feedback = g_strdup_printf ("update 0 0 %d %d %d %d %d %d %d %d",
                                  priv->surface_width, priv->surface_height,
                                  priv->surface_width, priv->surface_height,
                                  sx, sy, doc_width, doc_height);
      send_feedback (view, feedback);
      g_free (feedback);

      if (!priv->waiting_for_ack)
        moz_headless_freeze_updates (MOZ_HEADLESS (view->parent), TRUE);

      view->waiting_for_ack = TRUE;
      priv->waiting_for_ack ++;
    }
}

static void
cursor_changed_cb (MozHeadlessCursorType type,
                   MozHeadlessCursor *special,
                   gpointer data)
{
  ClutterMozHeadless *mozheadless = data;
  /* TODO - support special cursors */
  char *feedback = g_strdup_printf ("cursor %d", type);
  send_feedback_all (mozheadless, feedback);
  g_free (feedback);
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
separate_strings (gchar **strings, gint n_strings, gchar *string)
{
  gint i;
  gboolean success = TRUE;

  if (!string)
    return FALSE;

  strings[0] = string;
  for (i = 0; i < n_strings - 1; i++)
    {
      gchar *string = strchr (strings[i], ' ');
      if (!string)
        {
          success = FALSE;
          break;
        }
      string[0] = '\0';
      strings[i+1] = string + 1;
    }

  return success;
}

static gboolean
send_mack (ClutterMozHeadlessView *view)
{
  view->mack_source = 0;
  send_feedback (view, "mack");
  return FALSE;
}

static void
clutter_moz_headless_resize (ClutterMozHeadless *moz_headless)
{
  MozHeadless *headless = MOZ_HEADLESS (moz_headless);
  ClutterMozHeadlessPrivate *priv = moz_headless->priv;

  moz_headless_set_surface (headless, NULL, 0, 0, 0);
  moz_headless_set_size (headless,
                         priv->surface_width - 0,
                         priv->surface_height - 0);
  
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
process_command (ClutterMozHeadlessView *view, gchar *command)
{
  gchar *detail;
  
  ClutterMozHeadless *moz_headless = view->parent;
  ClutterMozHeadlessPrivate *priv = moz_headless->priv;
  MozHeadless *headless = MOZ_HEADLESS (moz_headless);
  
  /*g_debug ("Processing command: %s", command);*/
  
  /* TODO: Of course, we should make this a binary format - it's this way 
   *       to ease debugging.
   */
  
  detail = strchr (command, ' ');
  if (detail)
    {
      detail[0] = '\0';
      detail++;
    }

  if (priv->sync_call && g_str_equal (command, priv->sync_call))
    priv->sync_call = NULL;

  if (g_str_equal (command, "ack!"))
    {
      view->waiting_for_ack = FALSE;
      priv->waiting_for_ack --;

      if (priv->waiting_for_ack == 0)
        clutter_moz_headless_unfreeze (moz_headless);
    }
  else if (g_str_equal (command, "open"))
    {
      moz_headless_load_url (headless, detail);
    }
  else if (g_str_equal (command, "resize"))
    {
      gchar *params[2];
      if (!separate_strings (params, G_N_ELEMENTS (params), detail))
        return;
      
      priv->surface_width = atoi (params[0]);
      priv->surface_height = atoi (params[1]);

      if (priv->waiting_for_ack)
        priv->pending_resize = TRUE;
      else
        clutter_moz_headless_resize (moz_headless);
    }
  else if (g_str_equal (command, "motion"))
    {
      gint x, y;
      MozHeadlessModifier m;
      gchar *params[3];
      if (!separate_strings (params, G_N_ELEMENTS (params), detail))
        return;
      
      x = atoi (params[0]);
      y = atoi (params[1]);
      m = atoi (params[2]);
      
      moz_headless_motion (headless, x, y, m);
      
      /* This is done so that we definitely get to do any redrawing before we
       * send an acknowledgement.
       */
      if (!view->mack_source)
        view->mack_source =
          g_idle_add_full (G_PRIORITY_LOW, (GSourceFunc)send_mack, view, NULL);
      else
        g_warning ("Received a motion event before sending acknowledgement");
    }
  else if (g_str_equal (command, "button-press"))
    {
      gint x, y, b, c;
      MozHeadlessModifier m;
      gchar *params[5];
      if (!separate_strings (params, G_N_ELEMENTS (params), detail))
        return;
      
      x = atoi (params[0]);
      y = atoi (params[1]);
      b = atoi (params[2]);
      c = atoi (params[3]);
      m = atoi (params[4]);
      
      moz_headless_button_press (headless, x, y, b, c, m);
    }
  else if (g_str_equal (command, "button-release"))
    {
      gint x, y, b;
      MozHeadlessModifier m;
      gchar *params[4];
      if (!separate_strings (params, G_N_ELEMENTS (params), detail))
        return;
      
      x = atoi (params[0]);
      y = atoi (params[1]);
      b = atoi (params[2]);
      m = atoi (params[3]);
      
      moz_headless_button_release (headless, x, y, b, m);
    }
  else if (g_str_equal (command, "key-press"))
    {
      gchar *params[3];

      if (!separate_strings (params, G_N_ELEMENTS (params), detail))
        return;

      moz_headless_key_press (headless, (MozHeadlessKey)atoi (params[0]),
                              (gunichar)atoi (params[1]),
                              (MozHeadlessModifier)atoi (params[2]));
    }
  else if (g_str_equal (command, "key-release"))
    {
      gchar *params[2];

      if (!separate_strings (params, G_N_ELEMENTS (params), detail))
        return;

      moz_headless_key_release (headless, (MozHeadlessKey)atoi (params[0]),
                                (MozHeadlessModifier)atoi (params[1]));
    }
  else if (g_str_equal (command, "scroll"))
    {
      gint dx, dy;
      gchar *params[2];
      if (!separate_strings (params, G_N_ELEMENTS (params), detail))
        return;
      
      dx = atoi (params[0]);
      dy = atoi (params[1]);
      
      moz_headless_scroll (headless, dx, dy);
    }
  else if (g_str_equal (command, "can-go-back?"))
    {
      gchar *feedback =
        g_strdup_printf ("back %d", moz_headless_can_go_back (headless));
      send_feedback (view, feedback);
      g_free (feedback);
    }
  else if (g_str_equal (command, "can-go-forward?"))
    {
      gchar *feedback =
        g_strdup_printf ("forward %d", moz_headless_can_go_forward (headless));
      send_feedback (view, feedback);
      g_free (feedback);
    }
  else if (g_str_equal (command, "shm-name?"))
    {
      gchar *feedback = g_strdup_printf ("shm-name %s", priv->shm_name);
      send_feedback (view, feedback);
      g_free (feedback);
    }
  else if (g_str_equal (command, "drawing?"))
    {
    }
  else if (g_str_equal (command, "back"))
    {
      moz_headless_go_back (headless);
    }
  else if (g_str_equal (command, "forward"))
    {
      moz_headless_go_forward (headless);
    }
  else if (g_str_equal (command, "stop"))
    {
      moz_headless_stop_load (headless);
    }
  else if (g_str_equal (command, "refresh"))
    {
      moz_headless_reload (headless, MOZ_HEADLESS_FLAG_RELOADNORMAL);
    }
  else if (g_str_equal (command, "reload"))
    {
      moz_headless_reload (headless, MOZ_HEADLESS_FLAG_RELOADBYPASSCACHE);
    }
  else if (g_str_equal (command, "set-chrome"))
    {
      gchar *params[1];

      if (!separate_strings (params, G_N_ELEMENTS (params), detail))
        return;

      moz_headless_set_chrome_mask (headless, atoi (params[0]));
    }
  else if (g_str_equal (command, "toggle-chrome"))
    {
      guint32 chrome;
      gchar *params[1];

      if (!separate_strings (params, G_N_ELEMENTS (params), detail))
        return;

      chrome = moz_headless_get_chrome_mask (headless);
      chrome ^= atoi (params[0]);
      moz_headless_set_chrome_mask (headless, chrome);
    }
  else if (g_str_equal (command, "quit"))
    {
      send_feedback_all (moz_headless, "closed");
      g_object_unref (moz_headless);
    }
  else if (g_str_equal (command, "new-view"))
    {
      gchar *params[2];

      if (!separate_strings (params, G_N_ELEMENTS (params), detail))
        return;
      
      clutter_mozheadless_create_view (moz_headless,
                                       g_strdup (params[0]),
                                       g_strdup (params[1]));
    }
  else if (g_str_equal (command, "new-window-response") ||
           g_str_equal (command, "new-window"))
    {
      gchar *params[3];

      if (!separate_strings (params, G_N_ELEMENTS (params), detail))
        {
          g_free (priv->new_input_file);
          priv->new_input_file = NULL;
          g_free (priv->new_output_file);
          priv->new_output_file = NULL;
          g_free (priv->new_shm_name);
          priv->new_shm_name = NULL;
          return;
        }

      if (g_str_equal (command, "new-window"))
        {
          g_object_new (CLUTTER_TYPE_MOZHEADLESS,
                        "input", params[0],
                        "output", params[1],
                        "shm", params[2],
                        NULL);
        }
      else
        {
          priv->new_input_file = g_strdup (params[0]);
          priv->new_output_file = g_strdup (params[1]);
          priv->new_shm_name = g_strdup (params[2]);
        }
    }
#ifdef SUPPORT_PLUGINS
  else if (g_str_equal (command, "plugin-window"))
    {
      Window viewport_window = (Window)strtoul (detail, NULL, 10);
      moz_headless_set_plugin_window (MOZ_HEADLESS (moz_headless),
                                      viewport_window);
    }
#endif
  else
    {
      g_warning ("Unrecognised command: %s", command);
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
  GIOStatus status;
  gchar buf[4096];
  gsize length;

  GError *error = NULL;
  ClutterMozHeadless *moz_headless = view->parent;
  ClutterMozHeadlessPrivate *priv = moz_headless->priv;

  switch (condition) {
    case G_IO_IN :
      /* We've received a connection, remove the disconnect timeout */
      if (priv->connect_timeout_source)
        {
          g_source_remove (priv->connect_timeout_source);
          priv->connect_timeout_source = 0;
        }

      status = g_io_channel_read_chars (source, buf, sizeof (buf),
                                        &length, &error);
      if (status == G_IO_STATUS_NORMAL) {
        gsize current_length = 0;
        while (current_length < length)
          {
            gchar *command = &buf[current_length];
            current_length += strlen (&buf[current_length]) + 1;
            process_command (view, command);
          }
        return TRUE;
      } else if (status == G_IO_STATUS_ERROR) {
        g_warning ("Error reading from source: %s", error->message);
        g_error_free (error);
        break;
      } else if (status == G_IO_STATUS_EOF) {
        g_warning ("End of file");
      } else if (status == G_IO_STATUS_AGAIN) {
        return TRUE;
      } else {
        g_warning ("Unknown condition");
      }
      break;

    case G_IO_ERR :
      g_warning ("Error");
      break;

    case G_IO_NVAL :
      g_warning ("Invalid request");
      break;

    case G_IO_HUP :
      /* Don't warn on this, this is fine */
      /*g_warning ("Hung up");*/
      break;

    default :
      g_warning ("Unhandled IO condition");
      break;
  }

  /* Kill this head or disconnect the view */
  if ((priv->views) && (view == priv->views->data))
    g_object_unref (moz_headless);
  else
    disconnect_view (view);

  return FALSE;
}

static void
block_until_command (ClutterMozHeadless *moz_headless, const gchar *command)
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

int
main (int argc, char **argv)
{
  ClutterMozHeadless *moz_headless;

#ifdef SUPPORT_PLUGINS
  gtk_init (&argc, &argv);
#endif

 if (argc != 4)
    {
      printf ("Usage: %s <output pipe> <input pipe> <shm name>\n", argv[0]);
      return 1;
    }

  g_type_init ();

  /* Initialise mozilla */
  moz_headless_set_path (MOZHOME);
  moz_headless_set_comp_path (PKGDATADIR);
  moz_headless_set_directory (NS_APP_USER_MIMETYPES_50_FILE,
                              PKGDATADIR "/mimeTypes.rdf");

  clutter_mozheadless_history_init ();
  clutter_mozheadless_prefs_init ();

  moz_headless = g_object_new (CLUTTER_TYPE_MOZHEADLESS,
                               "output", argv[1],
                               "input", argv[2],
                               "shm", argv[3],
                               NULL);

  moz_headless_set_change_cursor_callback (cursor_changed_cb,
                                           moz_headless);

  /* Begin */
  mainloop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (mainloop);

  clutter_mozheadless_history_deinit ();
  clutter_mozheadless_prefs_deinit ();

  return 0;
}

