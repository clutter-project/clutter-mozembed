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
#include <nsError.h>

static guint
clutter_mozheadless_prefs_read_user (const gchar *file,
                                     gpointer     user_data)
{
  guint ns_result = NS_ERROR_UNEXPECTED;
  gboolean result;
  GError *error = NULL;

  g_debug ("ReadUserPrefs(%s)", file);
  result = places_prefs_read_user ((PlacesPrefs *)user_data,
                                   file,
                                   &ns_result,
                                   &error);

  if (!result)
    {
      g_warning ("Error reading user prefs: %s", error->message);
      g_error_free (error);
    }

  return ns_result;
}

static guint
clutter_mozheadless_prefs_reset (gpointer user_data)
{
  guint ns_result = NS_ERROR_UNEXPECTED;
  gboolean result;
  GError *error = NULL;

  g_debug ("ResetPrefs");
  result = places_prefs_reset ((PlacesPrefs *)user_data, &ns_result, &error);

  if (!result)
    {
      g_warning ("Error resetting prefs: %s", error->message);
      g_error_free (error);
    }

  return ns_result;
}

static guint
clutter_mozheadless_prefs_reset_user (gpointer user_data)
{
  guint ns_result = NS_ERROR_UNEXPECTED;
  gboolean result;
  GError *error = NULL;

  g_debug ("ResetUserPrefs");
  result = places_prefs_reset_user ((PlacesPrefs *)user_data,
                                    &ns_result,
                                    &error);

  if (!result)
    {
      g_warning ("Error resetting user prefs: %s", error->message);
      g_error_free (error);
    }

  return ns_result;
}

static guint
clutter_mozheadless_prefs_save_pref_file (const gchar *file,
                                          gpointer     user_data)
{
  guint ns_result = NS_ERROR_UNEXPECTED;
  gboolean result;
  GError *error = NULL;

  g_debug ("SavePrefFile(%s)", file);
  result = places_prefs_save_pref_file ((PlacesPrefs *)user_data,
                                        file,
                                        &ns_result,
                                        &error);

  if (!result)
    {
      g_warning ("Error saving prefs file: %s", error->message);
      g_error_free (error);
    }

  return ns_result;
}

static guint
clutter_mozheadless_prefs_get_branch (const gchar *root,
                                      gint        *id,
                                      gpointer     user_data)
{
  guint ns_result = NS_ERROR_UNEXPECTED;
  gboolean result;
  GError *error = NULL;

  g_debug ("GetBranch(%s)", root);
  result = places_prefs_get_branch ((PlacesPrefs *)user_data,
                                    root,
                                    id,
                                    &ns_result,
                                    &error);

  if (!result)
    {
      g_warning ("Error getting branch: %s", error->message);
      g_error_free (error);
    }

  return ns_result;
}

static guint
clutter_mozheadless_prefs_get_default_branch (const gchar *root,
                                              gint        *id,
                                              gpointer     user_data)
{
  guint ns_result = NS_ERROR_UNEXPECTED;
  gboolean result;
  GError *error = NULL;

  g_debug ("GetDefaultBranch(%s)", root);
  result = places_prefs_get_default_branch ((PlacesPrefs *)user_data,
                                            root,
                                            id,
                                            &ns_result,
                                            &error);

  if (!result)
    {
      g_warning ("Error getting default branch: %s", error->message);
      g_error_free (error);
    }

  return ns_result;
}

static guint
clutter_mozheadless_prefs_release_branch (gint     id,
                                          gpointer user_data)
{
  guint ns_result = NS_ERROR_UNEXPECTED;
  gboolean result;
  GError *error = NULL;

  result = places_prefs_release_branch ((PlacesPrefs *)user_data,
                                        id,
                                        &ns_result,
                                        &error);

  if (!result)
    {
      g_warning ("Error releasing branch: %s", error->message);
      g_error_free (error);
    }

  return ns_result;
}

static guint
clutter_mozheadless_prefs_branch_get_type (gint         id,
                                           const gchar *name,
                                           gint        *type,
                                           gpointer     user_data)
{
  guint ns_result = NS_ERROR_UNEXPECTED;
  gboolean result;
  GError *error = NULL;

  g_debug ("GetType(%d, %s)", id, name);
  result = places_prefs_branch_get_type ((PlacesPrefs *)user_data,
                                         id,
                                         name,
                                         type,
                                         &ns_result,
                                         &error);

  if (!result)
    {
      g_warning ("Error getting branch type: %s", error->message);
      g_error_free (error);
    }

  return ns_result;
}

static guint
clutter_mozheadless_prefs_branch_clear_user (gint         id,
                                             const gchar *name,
                                             gpointer     user_data)
{
  g_debug ("ClearUser(%d, %s)", id, name);
  return 0;
}

static guint
clutter_mozheadless_prefs_branch_has_user_value (gint         id,
                                                 const gchar *name,
                                                 gboolean    *has_value,
                                                 gpointer     user_data)
{
  guint ns_result = NS_ERROR_UNEXPECTED;
  gboolean result;
  GError *error = NULL;

  g_debug ("HasUserValue(%d, %s)", id, name);
  result = places_prefs_branch_has_user_value ((PlacesPrefs *)user_data,
                                               id,
                                               name,
                                               has_value,
                                               &ns_result,
                                               &error);

  if (!result)
    {
      g_warning ("Error getting branch has-user-value: %s", error->message);
      g_error_free (error);
    }

  return ns_result;
}

static guint
clutter_mozheadless_prefs_branch_delete (gint         id,
                                         const gchar *start,
                                         gpointer     user_data)
{
  g_debug ("Delete(%d, %s)", id, start);
  return (guint)0;
}

static guint
clutter_mozheadless_prefs_branch_reset (gint         id,
                                        const gchar *start,
                                        gpointer     user_data)
{
  g_debug ("Reset(%d, %s)", id, start);
  return (guint)0;
}

static guint
clutter_mozheadless_prefs_branch_get_child_list (gint         id,
                                                 const gchar *start,
                                                 guint       *len,
                                                 gchar     ***array,
                                                 gpointer     user_data)
{
  guint ns_result = NS_ERROR_UNEXPECTED;
  gboolean result;
  GError *error = NULL;

  g_debug ("GetChildList(%d, %s)", id, start);
  *len = 0;
  *array = NULL;
  result = places_prefs_branch_get_child_list ((PlacesPrefs *)user_data,
                                               id,
                                               start,
                                               len,
                                               array,
                                               &ns_result,
                                               &error);

  if (!result)
    {
      g_warning ("Error getting branch child list: %s", error->message);
      g_error_free (error);
    }

  return ns_result;
}

static guint
clutter_mozheadless_prefs_branch_lock (gint         id,
                                       const gchar *name,
                                       gpointer     user_data)
{
  guint ns_result = NS_ERROR_UNEXPECTED;
  gboolean result;
  GError *error = NULL;

  g_debug ("Lock(%d, %s)", id, name);
  result = places_prefs_branch_lock ((PlacesPrefs *)user_data,
                                     id,
                                     name,
                                     &ns_result,
                                     &error);

  if (!result)
    {
      g_warning ("Error locking branch: %s", error->message);
      g_error_free (error);
    }

  return ns_result;
}

static guint
clutter_mozheadless_prefs_branch_is_locked (gint         id,
                                            const gchar *name,
                                            gboolean    *value,
                                            gpointer     user_data)
{
  guint ns_result = NS_ERROR_UNEXPECTED;
  gboolean result;
  GError *error = NULL;

  g_debug ("IsLocked(%d, %s)", id, name);
  result = places_prefs_branch_is_locked ((PlacesPrefs *)user_data,
                                          id,
                                          name,
                                          value,
                                          &ns_result,
                                          &error);

  if (!result)
    {
      g_warning ("Error getting branch is-locked state: %s", error->message);
      g_error_free (error);
    }

  return ns_result;
}

static guint
clutter_mozheadless_prefs_branch_unlock (gint         id,
                                         const gchar *name,
                                         gpointer     user_data)
{
  guint ns_result = NS_ERROR_UNEXPECTED;
  gboolean result;
  GError *error = NULL;

  g_debug ("Unlock(%d, %s)", id, name);
  result = places_prefs_branch_unlock ((PlacesPrefs *)user_data,
                                       id,
                                       name,
                                       &ns_result,
                                       &error);

  if (!result)
    {
      g_warning ("Error unlocking branch: %s", error->message);
      g_error_free (error);
    }

  return ns_result;
}

static guint
clutter_mozheadless_prefs_branch_get_bool (gint         id,
                                           const gchar *name,
                                           gboolean    *value,
                                           gpointer     user_data)
{
  guint ns_result = NS_ERROR_UNEXPECTED;
  gboolean result;
  GError *error = NULL;

  g_debug ("GetBool(%d, %s)", id, name);
  result = places_prefs_branch_get_bool ((PlacesPrefs *)user_data,
                                         id,
                                         name,
                                         value,
                                         &ns_result,
                                         &error);

  if (!result)
    {
      g_warning ("Error getting branch boolean value: %s", error->message);
      g_error_free (error);
    }

  return ns_result;
}

static guint
clutter_mozheadless_prefs_branch_set_bool (gint         id,
                                           const gchar *name,
                                           gboolean     value,
                                           gpointer     user_data)
{
  guint ns_result = NS_ERROR_UNEXPECTED;
  gboolean result;
  GError *error = NULL;

  g_debug ("SetBool(%d, %s, %d)", id, name, value);
  result = places_prefs_branch_set_bool ((PlacesPrefs *)user_data,
                                         id,
                                         name,
                                         value,
                                         &ns_result,
                                         &error);

  if (!result)
    {
      g_warning ("Error setting branch boolean value: %s", error->message);
      g_error_free (error);
    }

  return ns_result;
}

static guint
clutter_mozheadless_prefs_branch_get_char (gint          id,
                                           const gchar  *name,
                                           gchar       **value,
                                           gpointer      user_data)
{
  guint ns_result = NS_ERROR_UNEXPECTED;
  gboolean result;
  GError *error = NULL;

  g_debug ("GetChar(%d, %s)", id, name);
  result = places_prefs_branch_get_char ((PlacesPrefs *)user_data,
                                         id,
                                         name,
                                         value,
                                         &ns_result,
                                         &error);

  if (!result)
    {
      g_warning ("Error getting branch char value: %s", error->message);
      g_error_free (error);
    }
  else
    {
      g_debug ("Got char value: %s", *value);
    }

  return ns_result;
}

static guint
clutter_mozheadless_prefs_branch_set_char (gint         id,
                                           const gchar *name,
                                           const gchar *value,
                                           gpointer     user_data)
{
  guint ns_result = NS_ERROR_UNEXPECTED;
  gboolean result;
  GError *error = NULL;

  g_debug ("SetChar(%d, %s, %s)", id, name, value);
  result = places_prefs_branch_set_char ((PlacesPrefs *)user_data,
                                         id,
                                         name,
                                         value,
                                         &ns_result,
                                         &error);

  if (!result)
    {
      g_warning ("Error setting branch char value: %s", error->message);
      g_error_free (error);
    }

  return ns_result;
}

static guint
clutter_mozheadless_prefs_branch_get_int (gint         id,
                                          const gchar *name,
                                          gint        *value,
                                          gpointer     user_data)
{
  guint ns_result = NS_ERROR_UNEXPECTED;
  gboolean result;
  GError *error = NULL;

  g_debug ("GetInt(%d, %s)", id, name);
  result = places_prefs_branch_get_int ((PlacesPrefs *)user_data,
                                        id,
                                        name,
                                        value,
                                        &ns_result,
                                        &error);

  if (!result)
    {
      g_warning ("Error getting branch int value: %s", error->message);
      g_error_free (error);
    }

  return ns_result;
}

static guint
clutter_mozheadless_prefs_branch_set_int (gint         id,
                                          const gchar *name,
                                          gint         value,
                                          gpointer     user_data)
{
  guint ns_result = NS_ERROR_UNEXPECTED;
  gboolean result;
  GError *error = NULL;

  g_debug ("SetInt(%d, %s, %d)", id, name, value);
  result = places_prefs_branch_set_int ((PlacesPrefs *)user_data,
                                        id,
                                        name,
                                        value,
                                        &ns_result,
                                        &error);

  if (!result)
    {
      g_warning ("Error setting branch int value: %s", error->message);
      g_error_free (error);
    }

  return ns_result;
}

static PlacesPrefs *prefs = NULL;

void
clutter_mozheadless_prefs_init ()
{
  if (!prefs)
    prefs = places_prefs_new ();

  if (prefs)
    {
      moz_headless_set_prefs_data (prefs);

      moz_headless_set_prefs_callbacks (
        clutter_mozheadless_prefs_read_user,
        clutter_mozheadless_prefs_reset,
        clutter_mozheadless_prefs_reset_user,
        clutter_mozheadless_prefs_save_pref_file,
        clutter_mozheadless_prefs_get_branch,
        clutter_mozheadless_prefs_get_default_branch,
        clutter_mozheadless_prefs_release_branch);

      moz_headless_set_prefs_branch_base_callbacks (
        clutter_mozheadless_prefs_branch_get_type,
        clutter_mozheadless_prefs_branch_clear_user,
        clutter_mozheadless_prefs_branch_has_user_value,
        clutter_mozheadless_prefs_branch_delete,
        clutter_mozheadless_prefs_branch_reset,
        clutter_mozheadless_prefs_branch_get_child_list);

      moz_headless_set_prefs_branch_lock_callbacks (
        clutter_mozheadless_prefs_branch_lock,
        clutter_mozheadless_prefs_branch_is_locked,
        clutter_mozheadless_prefs_branch_unlock);

      moz_headless_set_prefs_branch_bool_callbacks (
        clutter_mozheadless_prefs_branch_get_bool,
        clutter_mozheadless_prefs_branch_set_bool);

      moz_headless_set_prefs_branch_char_callbacks (
        clutter_mozheadless_prefs_branch_get_char,
        clutter_mozheadless_prefs_branch_set_char);

      moz_headless_set_prefs_branch_int_callbacks (
        clutter_mozheadless_prefs_branch_get_int,
        clutter_mozheadless_prefs_branch_set_int);
    }
}

void
clutter_mozheadless_prefs_deinit ()
{
  if (prefs)
    {
      g_object_unref (G_OBJECT (prefs));
      prefs = NULL;
    }
}

