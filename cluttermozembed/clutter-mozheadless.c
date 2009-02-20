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

G_DEFINE_TYPE (ClutterMozHeadless, clutter_mozheadless, MOZ_TYPE_HEADLESS)

#define MOZHEADLESS_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), CLUTTER_TYPE_MOZHEADLESS, ClutterMozHeadlessPrivate))

enum
{
  PROP_0,
 
  PROP_INPUT,
  PROP_OUTPUT,
  PROP_SHM,
};

struct _ClutterMozHeadlessPrivate
{
  gchar           *input_file;
  gchar           *output_file;
  gchar           *shm_name;
  GIOChannel      *input;
  GIOChannel      *output;
  guint            watch_id;
  GFileMonitor    *monitor;

  gboolean         waiting_for_ack;
  gboolean         missed_update;
  int              shm_fd;
  void            *mmap_start;
  size_t           mmap_length;

  gint             last_x;
  gint             last_y;
  gint             last_width;
  gint             last_height;

  gint             surface_width;
  gint             surface_height;

  /* Variables for synchronous calls */
  const gchar     *sync_call;
  gchar           *new_input_file;
  gchar           *new_output_file;
  gchar           *new_shm_name;
};

static GMainLoop *mainloop;

static void block_until_command (ClutterMozHeadless *moz_headless,
                                 const gchar        *command);

static void
send_feedback (ClutterMozHeadless *headless, const gchar *feedback)
{
  ClutterMozHeadlessPrivate *priv = headless->priv;
  g_io_channel_write_chars (priv->output, feedback,
                            strlen (feedback) + 1, NULL, NULL);
  g_io_channel_flush (priv->output, NULL);
}

static void
location_cb (ClutterMozHeadless *headless)
{
  gchar *location, *feedback;
  
  location = moz_headless_get_location (MOZ_HEADLESS (headless));
  feedback = g_strdup_printf ("location %s", location);
  
  send_feedback (headless, feedback);
  
  g_free (feedback);
  g_free (location);
}

static void
title_cb (ClutterMozHeadless *headless)
{
  gchar *title, *feedback;
  
  title = moz_headless_get_title (MOZ_HEADLESS (headless));
  feedback = g_strdup_printf ("title %s", title);
  
  send_feedback (headless, feedback);
  
  g_free (feedback);
  g_free (title);
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
  
  send_feedback (headless, feedback);
  
  g_free (feedback);
}

static void
net_start_cb (ClutterMozHeadless *headless)
{
  send_feedback (headless, "net-start");
}

static void
net_stop_cb (ClutterMozHeadless *headless)
{
  send_feedback (headless, "net-stop");
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
  static gint n_missed_updates = 0;
  /*gint doc_width, doc_height, sx, sy*/;
  gchar *feedback;
  
  ClutterMozHeadlessPrivate *priv = CLUTTER_MOZHEADLESS (headless)->priv;
  
  if (priv->waiting_for_ack)
    {
      if (!priv->missed_update)
        {
          priv->last_x = x;
          priv->last_y = y;
          priv->last_width = width;
          priv->last_height = height;
          priv->missed_update = TRUE;
        }
      else
        {
          n_missed_updates ++;

          if (x + width > priv->last_x + priv->last_width)
            priv->last_width = (x + width) - priv->last_x;
          if (y + height > priv->last_y + priv->last_height)
            priv->last_height = (y + height) - priv->last_y;
          if (x < priv->last_x)
            {
              priv->last_width += priv->last_x - x;
              priv->last_x = x;
            }
          if (y < priv->last_y)
            {
              priv->last_height += priv->last_y - y;
              priv->last_y = y;
            }
        }
      
      return;
    }
  
  /*g_debug ("Update +%d+%d %dx%d", x, y, width, height);*/
  /*moz_headless_get_document_size (headless, &doc_width, &doc_height);*/
  /*g_debug ("Doc-size: %dx%d", doc_width, doc_height);*/
  
  priv->missed_update = FALSE;
  if (n_missed_updates) {
    g_debug ("%d missed updates", n_missed_updates);
    n_missed_updates = 0;
  }
  
  msync (priv->mmap_start, priv->mmap_length, MS_SYNC);

  /*moz_headless_get_scroll_pos (headless, &sx, &sy);*/
  feedback = g_strdup_printf ("update %d %d %d %d %d %d",
                              x, y, width, height,
                              priv->surface_width, priv->surface_height);
  send_feedback (CLUTTER_MOZHEADLESS (headless), feedback);
  g_free (feedback);
  
  priv->waiting_for_ack = TRUE;
  moz_headless_freeze_updates (headless, TRUE);
}

static void
new_window_cb (MozHeadless *headless, MozHeadless **newEmbed, guint chromemask)
{
  gchar *feedback;

  ClutterMozHeadless *moz_headless = CLUTTER_MOZHEADLESS (headless);
  ClutterMozHeadlessPrivate *priv = moz_headless->priv;

  g_debug ("Creating new window (possibly)");
  
  feedback = g_strdup_printf ("new-window? %d", chromemask);
  send_feedback (moz_headless, feedback);
  g_free (feedback);
  g_debug ("Waiting for response");
  block_until_command (moz_headless, "new-window-response");
  g_debug ("Got response");

  if (priv->new_input_file && priv->new_output_file && priv->new_shm_name)
    {
      *newEmbed = g_object_new (CLUTTER_TYPE_MOZHEADLESS,
                                "output", priv->new_output_file,
                                "input", priv->new_input_file,
                                "shm", priv->new_shm_name,
                                NULL);
      g_debug ("Created window");
    }

  g_free (priv->new_input_file);
  g_free (priv->new_output_file);
  g_free (priv->new_shm_name);

  priv->new_input_file = NULL;
  priv->new_output_file = NULL;
  priv->new_shm_name = NULL;
}

static gboolean
separate_strings (gchar **strings, gint n_strings, gchar *string)
{
  gint i;
  gboolean success = TRUE;
  
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
send_mack (ClutterMozHeadless *moz_headless)
{
  send_feedback (moz_headless, "mack");
  return FALSE;
}

static void
process_command (ClutterMozHeadless *moz_headless, gchar *command)
{
  gchar *detail;
  
  ClutterMozHeadlessPrivate *priv = moz_headless->priv;
  MozHeadless *headless = MOZ_HEADLESS (moz_headless);
  
  /*g_debug ("Command: %s", command);*/
  
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

  if (g_str_equal (command, "ack"))
    {
      priv->waiting_for_ack = FALSE;
      moz_headless_freeze_updates (headless, FALSE);
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
      g_idle_add_full (G_PRIORITY_LOW, (GSourceFunc)send_mack, headless, NULL);
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
      send_feedback (moz_headless, feedback);
      g_free (feedback);
    }
  else if (g_str_equal (command, "can-go-forward?"))
    {
      gchar *feedback =
        g_strdup_printf ("forward %d", moz_headless_can_go_forward (headless));
      send_feedback (moz_headless, feedback);
      g_free (feedback);
    }
  else if (g_str_equal (command, "shm-name?"))
    {
      gchar *feedback = g_strdup_printf ("shm-name %s", priv->shm_name);
      send_feedback (moz_headless, feedback);
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
  else if (g_str_equal (command, "quit"))
    {
      g_main_loop_quit (mainloop);
    }
  else if (g_str_equal (command, "new-window-response"))
    {
      gchar *params[3];

      if (!separate_strings (params, G_N_ELEMENTS (params), detail))
        return;
      
      priv->new_input_file = g_strdup (params[0]);
      priv->new_output_file = g_strdup (params[1]);
      priv->new_shm_name = g_strdup (params[2]);
    }
  else
    {
      g_warning ("Unrecognised command: %s", command);
    }
}

static gboolean
input_io_func (GIOChannel          *source,
               GIOCondition         condition,
               ClutterMozHeadless  *moz_headless)
{
  /* FYI: Maximum URL length in IE is 2083 characters */
  gchar buf[4096];
  gsize length;

  GError *error = NULL;
  
  switch (condition) {
    case G_IO_IN :
      if (g_io_channel_read_chars (source, buf, sizeof (buf), &length, &error)
          == G_IO_STATUS_NORMAL) {
        gsize current_length = 0;
        while (current_length < length)
          {
            gchar *command = &buf[current_length];
            current_length += strlen (&buf[current_length]) + 1;
            process_command (moz_headless, command);
          }
      } else {
        g_warning ("Error reading from source: %s", error->message);
        g_error_free (error);
      }
      return TRUE;

    case G_IO_ERR :
      g_warning ("Error");
      break;
    
    case G_IO_NVAL :
      g_warning ("Invalid request");
      break;
    
    case G_IO_HUP :
      g_warning ("Hung up");
      break;
    
    default :
      g_warning ("Unhandled IO condition");
      break;
  }

  g_main_loop_quit (mainloop);

  return FALSE;
}

static void
block_until_command (ClutterMozHeadless *moz_headless, const gchar *command)
{
  ClutterMozHeadlessPrivate *priv = moz_headless->priv;
  
  priv->sync_call = command;
  
  /* FIXME: There needs to be a time limit here, or we can hang if the front-end
   *        hangs. Here or in input_io_func anyway...
   */
  while (input_io_func (priv->input, G_IO_IN, moz_headless) && priv->sync_call);

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

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
clutter_mozheadless_dispose (GObject *object)
{
  ClutterMozHeadlessPrivate *priv = CLUTTER_MOZHEADLESS (object)->priv;
  
  if (priv->monitor)
    {
      g_file_monitor_cancel (priv->monitor);
      g_object_unref (priv->monitor);
      priv->monitor = NULL;
    }

  if (priv->input)
    {
      GError *error = NULL;

      if (priv->watch_id) {
        g_source_remove (priv->watch_id);
        priv->watch_id = 0;
      }
      
      if (g_io_channel_shutdown (priv->input, FALSE, &error) ==
          G_IO_STATUS_ERROR)
        {
          g_warning ("Error closing input channel: %s", error->message);
          g_error_free (error);
        }
      
      g_io_channel_unref (priv->input);
      priv->input = NULL;
      g_remove (priv->input_file);
    }

  if (priv->output)
    {
      GError *error = NULL;

      if (g_io_channel_shutdown (priv->output, FALSE, &error) ==
          G_IO_STATUS_ERROR)
        {
          g_warning ("Error closing output channel: %s", error->message);
          g_error_free (error);
        }
      
      g_io_channel_unref (priv->output);
      priv->output = NULL;
      g_remove (priv->output_file);
    }
  
  G_OBJECT_CLASS (clutter_mozheadless_parent_class)->dispose (object);
}

static void
clutter_mozheadless_finalize (GObject *object)
{
  ClutterMozHeadlessPrivate *priv = CLUTTER_MOZHEADLESS (object)->priv;
  
  shm_unlink (priv->shm_name);

  g_free (priv->output_file);
  g_free (priv->input_file);
  g_free (priv->shm_name);
  
  G_OBJECT_CLASS (clutter_mozheadless_parent_class)->finalize (object);
}

static void
file_changed_cb (GFileMonitor       *monitor,
                 GFile              *file,
                 GFile              *other_file,
                 GFileMonitorEvent   event_type,
                 ClutterMozHeadless *self)
{
  ClutterMozHeadlessPrivate *priv = self->priv;

  if (event_type != G_FILE_MONITOR_EVENT_CREATED)
    return;
  
  g_signal_handlers_disconnect_by_func (monitor, file_changed_cb, self);
  g_file_monitor_cancel (monitor);
  g_object_unref (monitor);
  priv->monitor = NULL;

  /* Opening input channel */
  g_debug ("Opening input file (%s)", priv->input_file);
  priv->input = g_io_channel_new_file (priv->input_file, "r", NULL);
  g_io_channel_set_encoding (priv->input, NULL, NULL);
  g_io_channel_set_buffered (priv->input, FALSE);
  g_io_channel_set_close_on_unref (priv->input, TRUE);
  g_io_add_watch (priv->input,
                  G_IO_IN | G_IO_ERR | G_IO_NVAL | G_IO_HUP,
                  (GIOFunc)input_io_func,
                  self);
  g_debug ("Opened input file");
}

static void
clutter_mozheadless_constructed (GObject *object)
{
  static gint spawned_heads = 0;
  
  GFile *file;

  ClutterMozHeadless *self = CLUTTER_MOZHEADLESS (object);
  ClutterMozHeadlessPrivate *priv = self->priv;
  
  g_debug ("Opening output file (%s)", priv->output_file);
  priv->output = g_io_channel_new_file (priv->output_file, "w", NULL);
  g_io_channel_set_encoding (priv->output, NULL, NULL);
  g_io_channel_set_buffered (priv->output, FALSE);
  g_io_channel_set_close_on_unref (priv->output, TRUE);
  g_debug ("Opened output file");

  file = g_file_new_for_path (priv->input_file);
  priv->monitor = g_file_monitor_file (file, 0, NULL, NULL);
  g_object_unref (file);
  g_signal_connect (priv->monitor, "changed",
                    G_CALLBACK (file_changed_cb), object);

  if (g_file_test (priv->input_file, G_FILE_TEST_EXISTS))
    file_changed_cb (priv->monitor, NULL, NULL,
                     G_FILE_MONITOR_EVENT_CREATED, self);
  
  if (!priv->shm_name)
    {
      priv->shm_name = g_strdup_printf ("/mozheadless-%d-%d",
                                        getpid (),
                                        spawned_heads);
    }
  priv->shm_fd = shm_open (priv->shm_name, O_CREAT | O_RDWR | O_TRUNC, 0666);
  if (priv->shm_fd == -1)
    g_error ("Error opening shared memory");
  
  /* Remove scrollbars */
  /*moz_headless_set_chrome_mask (headless, 0);*/

  /*moz_headless_set_surface_offset (headless, 50, 50);*/
  
  g_signal_connect (object, "location",
                    G_CALLBACK (location_cb), NULL);
  g_signal_connect (object, "title",
                    G_CALLBACK (title_cb), NULL);
  g_signal_connect (object, "progress",
                    G_CALLBACK (progress_cb), NULL);
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

  spawned_heads ++;
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
}

static void
clutter_mozheadless_init (ClutterMozHeadless *self)
{
  /*ClutterMozHeadlessPrivate *priv = */self->priv = MOZHEADLESS_PRIVATE (self);
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

  g_type_init ();
  
  if (argc != 4)
    {
      printf ("Usage: %s <output pipe> <input pipe> <shm name>\n", argv[0]);
      return 1;
    }
  
  /* Initialise mozilla */
  moz_headless_set_path (MOZHOME);
  
  moz_headless = g_object_new (CLUTTER_TYPE_MOZHEADLESS,
                               "output", argv[1],
                               "input", argv[2],
                               "shm", argv[3],
                               NULL);

  /* Begin */
  mainloop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (mainloop);
  
  return 0;
}

