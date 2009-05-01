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

#include "clutter-mozheadless.h"
#include "clutter-mozheadless-prefs.h"
#include <places-glib/places-glib.h>

static void
clutter_mozheadless_prefs_read_user (const gchar *file,
                                     gpointer     user_data)
{
  g_debug ("ReadUserPrefs(%s)", file);
}

static void
clutter_mozheadless_prefs_reset (gpointer user_data)
{
  g_debug ("ResetPrefs");
}

static void
clutter_mozheadless_prefs_reset_user (gpointer user_data)
{
  g_debug ("ResetUserPrefs");
}

static void
clutter_mozheadless_prefs_save_pref_file (const gchar *file,
                                          gpointer     user_data)
{
  g_debug ("SavePrefFile(%s)", file);
}

static gint
clutter_mozheadless_prefs_get_branch (const gchar *root,
                                      gpointer     user_data)
{
  g_debug ("GetBranch(%s)", root);
  return -1;
}

static gint
clutter_mozheadless_prefs_get_default_branch (const gchar *root,
                                              gpointer     user_data)
{
  g_debug ("GetDefaultBranch(%s)", root);
  return -1;
}

static gint
clutter_mozheadless_prefs_branch_get_type (gint         id,
                                           const gchar *name,
                                           gpointer     user_data)
{
  g_debug ("GetType(%d, %s)", id, name);
  return 0;
}

static void
clutter_mozheadless_prefs_branch_clear_user (gint         id,
                                             const gchar *name,
                                             gpointer     user_data)
{
  g_debug ("ClearUser(%d, %s)", id, name);
}

static gboolean
clutter_mozheadless_prefs_branch_has_user_value (gint         id,
                                                 const gchar *name,
                                                 gpointer     user_data)
{
  g_debug ("HasUserValue(%d, %s)", id, name);
  return FALSE;
}

static void
clutter_mozheadless_prefs_branch_delete (gint         id,
                                         const gchar *start,
                                         gpointer     user_data)
{
  g_debug ("Delete(%d, %s)", id, start);
}

static void
clutter_mozheadless_prefs_branch_reset (gint         id,
                                        const gchar *start,
                                        gpointer     user_data)
{
  g_debug ("Reset(%d, %s)", id, start);
}

static void
clutter_mozheadless_prefs_branch_get_child_list (gint         id,
                                                 const gchar *start,
                                                 guint       *len,
                                                 gchar     ***array,
                                                 gpointer     user_data)
{
  g_debug ("GetChildList(%d, %s)", id, start);
  *len = 0;
  *array = NULL;
}

void
clutter_mozheadless_prefs_init ()
{
  moz_headless_set_prefs_callbacks (
    clutter_mozheadless_prefs_read_user,
    clutter_mozheadless_prefs_reset,
    clutter_mozheadless_prefs_reset_user,
    clutter_mozheadless_prefs_save_pref_file,
    clutter_mozheadless_prefs_get_branch,
    clutter_mozheadless_prefs_get_default_branch);

  moz_headless_set_prefs_branch_base_callbacks (
    clutter_mozheadless_prefs_branch_get_type,
    clutter_mozheadless_prefs_branch_clear_user,
    clutter_mozheadless_prefs_branch_has_user_value,
    clutter_mozheadless_prefs_branch_delete,
    clutter_mozheadless_prefs_branch_reset,
    clutter_mozheadless_prefs_branch_get_child_list);
}

void
clutter_mozheadless_prefs_deinit ()
{
}

