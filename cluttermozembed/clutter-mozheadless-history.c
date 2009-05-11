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

#include "clutter-mozheadless-history.h"
#include <moz-headless.h>
#include <mhs/mhs.h>

static void
clutter_mozheadless_history_add_uri (const gchar *uri,
                                     gboolean     redirect,
                                     gboolean     top_level,
                                     const gchar *referrer,
                                     gpointer     user_data)
{
  GError *error = NULL;
  gboolean result =
    mhs_history_add_uri ((MhsHistory *)user_data,
                         uri,
                         redirect,
                         top_level,
                         referrer,
                         &error);
  if (!result)
    {
      g_warning ("Error adding URI: %s", error->message);
      g_error_free (error);
    }
}

static gboolean
clutter_mozheadless_history_is_visited (const gchar *uri, gpointer user_data)
{
  GError *error = NULL;
  gboolean is_visited = FALSE;
  gboolean result =
    mhs_history_is_visited ((MhsHistory *)user_data,
                            uri,
                            &is_visited,
                            &error);
  if (!result)
    {
      g_warning ("Error checking is-visited: %s", error->message);
      g_error_free (error);
    }

  return is_visited;
}

static void
clutter_mozheadless_history_set_page_title (const gchar *uri,
                                            const gchar *title,
                                            gpointer     user_data)
{
  GError *error = NULL;
  gboolean result =
    mhs_history_set_page_title ((MhsHistory *)user_data,
                                uri,
                                title,
                                &error);
  if (!result)
    {
      g_warning ("Error setting page title: %s", error->message);
      g_error_free (error);
    }
}

static void
_link_visited_cb (MhsHistory *history, const gchar *uri)
{
  moz_headless_history_send_link_visited (uri);
}

static MhsHistory *history = NULL;

void
clutter_mozheadless_history_init ()
{
  if (!history)
    history = mhs_history_new ();

  if (history)
    {
      moz_headless_set_history_data (history);
      moz_headless_set_history_callbacks (
        clutter_mozheadless_history_add_uri,
        clutter_mozheadless_history_is_visited,
        clutter_mozheadless_history_set_page_title);
      g_signal_connect (history, "link-visited",
                        G_CALLBACK (_link_visited_cb), NULL);
    }
}

void
clutter_mozheadless_history_deinit ()
{
  if (history)
    {
      g_signal_handlers_disconnect_by_func (history, _link_visited_cb, NULL);
      g_object_unref (G_OBJECT (history));
      history = NULL;
    }
}

