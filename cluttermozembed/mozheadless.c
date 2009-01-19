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

/* Building */
/* gcc -o mozheadless mozheadless.c -Wall -g `pkg-config --cflags --libs glib-2.0 sqlite3` -lxpcomglue_s -lxul -lxpcom -L/home/cwiiis/Projects/mozilla/obj-i686-pc-linux-gnu/dist/lib -I/home/cwiiis/Projects/mozilla/obj-i686-pc-linux-gnu/dist/include -I/home/cwiiis/Projects/mozilla/obj-i686-pc-linux-gnu/dist/include/nspr -lmozjs -lsoftokn3 */

#include <glib.h>
#include <glib/gstdio.h>
#include <moz-headless.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>

static GIOChannel *input = NULL;
static FILE *output = NULL;
static MozHeadless *headless;
static GMainLoop *mainloop;

static gboolean waiting_for_ack = FALSE;
static gboolean missed_update = FALSE;
static int shm_fd = -1;
static void *mmap_start = NULL;
static size_t mmap_length = 0;

static gint last_x, last_y, last_width, last_height;
static gint surface_width, surface_height;

void
send_feedback (const gchar *feedback)
{
  fwrite (feedback, strlen (feedback) + 1, 1, output);
  fflush (output);
}

static void
location_cb (MozHeadless *headless)
{
  gchar *location, *feedback;
  
  location = moz_headless_get_location (headless);
  feedback = g_strdup_printf ("location %s", location);
  
  send_feedback (feedback);
  
  g_free (feedback);
  g_free (location);
}

static void
title_cb (MozHeadless *headless)
{
  gchar *title, *feedback;
  
  title = moz_headless_get_title (headless);
  feedback = g_strdup_printf ("title %s", title);
  
  send_feedback (feedback);
  
  g_free (feedback);
  g_free (title);
}

static void
progress_cb (MozHeadless *headless, gint curprogress, gint maxprogress)
{
  gchar *feedback;
  gdouble progress;
  
  progress = curprogress / (gdouble)maxprogress;
  feedback = g_strdup_printf ("progress %lf", progress);
  
  send_feedback (feedback);
  
  g_free (feedback);
}

static void
net_start_cb (MozHeadless *headless)
{
  send_feedback ("net-start");
}

static void
net_stop_cb (MozHeadless *headless)
{
  send_feedback ("net-stop");
}

static gboolean
scroll_cb (MozHeadless *headless, gint dx, gint dy)
{
  return TRUE;
}

static void
updated_cb (MozHeadless *headless, gint x, gint y, gint width, gint height)
{
  static gint n_missed_updates = 0;
  /*gint doc_width, doc_height, sx, sy*/;
  gchar *feedback;
  
  if (waiting_for_ack)
    {
      if (!missed_update)
        {
          last_x = x;
          last_y = y;
          last_width = width;
          last_height = height;
          missed_update = TRUE;
        }
      else
        {
          n_missed_updates ++;

          if (x + width > last_x + last_width)
            last_width = (x + width) - last_x;
          if (y + height > last_y + last_height)
            last_height = (y + height) - last_y;
          if (x < last_x)
            {
              last_width += last_x - x;
              last_x = x;
            }
          if (y < last_y)
            {
              last_height += last_y - y;
              last_y = y;
            }
        }
      
      return;
    }
  
  /*g_debug ("Update +%d+%d %dx%d", x, y, width, height);*/
  /*moz_headless_get_document_size (headless, &doc_width, &doc_height);*/
  /*g_debug ("Doc-size: %dx%d", doc_width, doc_height);*/
  
  missed_update = FALSE;
  if (n_missed_updates) {
    g_debug ("%d missed updates", n_missed_updates);
    n_missed_updates = 0;
  }
  
  msync (mmap_start, mmap_length, MS_SYNC);

  /*moz_headless_get_scroll_pos (headless, &sx, &sy);*/
  feedback = g_strdup_printf ("update %d %d %d %d",
                              x, y, width, height/*, sx, sy*/);
  send_feedback (feedback);
  g_free (feedback);
  
  waiting_for_ack = TRUE;
  moz_headless_freeze_updates (headless, TRUE);
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
send_mack ()
{
  send_feedback ("mack");
  return FALSE;
}

static void
process_command (gchar *command)
{
  gchar *detail;
  
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

  if (g_str_equal (command, "ack"))
    {
      waiting_for_ack = FALSE;
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
      
      surface_width = atoi (params[0]);
      surface_height = atoi (params[1]);
      
      moz_headless_set_surface (headless, NULL, 0, 0, 0);
      moz_headless_set_size (headless, surface_width - 0, surface_height - 0);
      
      if (mmap_start)
        munmap (mmap_start, mmap_length);

      mmap_length = surface_width * surface_height * 4;
      ftruncate (shm_fd, mmap_length);
      mmap_start = mmap (NULL, mmap_length, PROT_READ | PROT_WRITE,
                         MAP_SHARED, shm_fd, 0);
      
      moz_headless_set_surface (headless, mmap_start,
                                surface_width, surface_height,
                                surface_width * 4);
    }
  else if (g_str_equal (command, "motion"))
    {
      gint x, y;
      gchar *params[2];
      if (!separate_strings (params, G_N_ELEMENTS (params), detail))
        return;
      
      x = atoi (params[0]);
      y = atoi (params[1]);
      
      moz_headless_motion (headless, x, y);
      
      /* This is done so that we definitely get to do any redrawing before we
       * send an acknowledgement.
       */
      g_idle_add_full (G_PRIORITY_LOW, (GSourceFunc)send_mack, NULL, NULL);
    }
  else if (g_str_equal (command, "button-press"))
    {
      gint x, y, b, c;
      gchar *params[4];
      if (!separate_strings (params, G_N_ELEMENTS (params), detail))
        return;
      
      x = atoi (params[0]);
      y = atoi (params[1]);
      b = atoi (params[2]);
      c = atoi (params[3]);
      
      moz_headless_button_press (headless, x, y, b, c);
    }
  else if (g_str_equal (command, "button-release"))
    {
      gint x, y, b;
      gchar *params[3];
      if (!separate_strings (params, G_N_ELEMENTS (params), detail))
        return;
      
      x = atoi (params[0]);
      y = atoi (params[1]);
      b = atoi (params[2]);
      
      moz_headless_button_release (headless, x, y, b);
    }
  else if (g_str_equal (command, "key-press"))
    {
      gint key = atoi (detail);
      moz_headless_key_press (headless, (MozHeadlessKey)key);
    }
  else if (g_str_equal (command, "key-release"))
    {
      gint key = atoi (detail);
      moz_headless_key_release (headless, (MozHeadlessKey)key);
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
      send_feedback (feedback);
      g_free (feedback);
    }
  else if (g_str_equal (command, "can-go-forward?"))
    {
      gchar *feedback =
        g_strdup_printf ("forward %d", moz_headless_can_go_forward (headless));
      send_feedback (feedback);
      g_free (feedback);
    }
  else if (g_str_equal (command, "back"))
    {
      moz_headless_go_back (headless);
    }
  else if (g_str_equal (command, "forward"))
    {
      moz_headless_go_forward (headless);
    }
  else if (g_str_equal (command, "quit"))
    {
      g_main_loop_quit (mainloop);
    }
  else
    {
      g_warning ("Unrecognised command: %s", command);
    }
}

static gboolean
input_io_func (GIOChannel *source, GIOCondition condition)
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
            process_command (command);
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

int
main (int argc, char **argv)
{
  g_type_init ();
  
  if (argc != 3)
    {
      printf ("Usage: %s <named pipe> <shm name>\n", argv[0]);
      return 1;
    }
  
  output = fopen (argv[1], "w");
  if (!output)
    g_error ("Error opening output channel");
  
  input = g_io_channel_unix_new (STDIN_FILENO);
  if (!input)
    g_error ("Error opening stdin");
  
  g_io_channel_set_encoding (input, NULL, NULL);
  g_io_channel_set_buffered (input, FALSE);
  g_io_channel_set_close_on_unref (input, TRUE);
  g_io_add_watch (input,
                  G_IO_IN | G_IO_ERR | G_IO_NVAL | G_IO_HUP,
                  (GIOFunc)input_io_func,
                  NULL);
  
  shm_fd = shm_open (argv[2], O_CREAT | O_RDWR | O_TRUNC, 0666);
  if (shm_fd == -1)
    g_error ("Error opening shared memory");
  
  /* Initialise mozilla */
  moz_headless_set_path (MOZHOME);
  
  headless = moz_headless_new ();
  
  /* Remove scrollbars */
  /*moz_headless_set_chrome_mask (headless, 0);*/

  /*moz_headless_set_surface_offset (headless, 50, 50);*/
  
  g_signal_connect (headless, "location",
                    G_CALLBACK (location_cb), NULL);
  g_signal_connect (headless, "title",
                    G_CALLBACK (title_cb), NULL);
  g_signal_connect (headless, "progress",
                    G_CALLBACK (progress_cb), NULL);
  g_signal_connect (headless, "net-start",
                    G_CALLBACK (net_start_cb), NULL);
  g_signal_connect (headless, "net-stop",
                    G_CALLBACK (net_stop_cb), NULL);
  g_signal_connect (headless, "scroll",
                    G_CALLBACK (scroll_cb), NULL);
  g_signal_connect (headless, "updated",
                    G_CALLBACK (updated_cb), NULL);

  /* Begin */
  mainloop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (mainloop);
  
  /* Clean up */
  fclose (output);
  g_remove (argv[1]);
  
  g_io_channel_shutdown (input, FALSE, NULL);
  g_io_channel_unref (input);
  
  shm_unlink ("/mozheadless");

  return 0;
}

