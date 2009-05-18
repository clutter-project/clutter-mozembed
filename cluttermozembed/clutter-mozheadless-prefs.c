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
#include <mhs/mhs.h>
#include <nsError.h>

static guint
clutter_mozheadless_prefs_read_user (const gchar *file,
                                     gpointer     user_data)
{
  guint ns_result = NS_ERROR_UNEXPECTED;
  gboolean result;
  GError *error = NULL;

  // g_debug ("ReadUserPrefs(%s)", file);
  result = mhs_prefs_read_user ((MhsPrefs *)user_data,
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

  // g_debug ("ResetPrefs");
  result = mhs_prefs_reset ((MhsPrefs *)user_data, &ns_result, &error);

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

  // g_debug ("ResetUserPrefs");
  result = mhs_prefs_reset_user ((MhsPrefs *)user_data,
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

  // g_debug ("SavePrefFile(%s)", file);
  result = mhs_prefs_save_pref_file ((MhsPrefs *)user_data,
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

  // g_debug ("GetBranch(%s)", root);
  result = mhs_prefs_get_branch ((MhsPrefs *)user_data,
                                 root,
                                 id,
                                 &ns_result,
                                 &error);

  if (!result)
    {
      /*g_warning ("Error getting branch: %s", error->message);*/
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

  // g_debug ("GetDefaultBranch(%s)", root);
  result = mhs_prefs_get_default_branch ((MhsPrefs *)user_data,
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

  result = mhs_prefs_release_branch ((MhsPrefs *)user_data,
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

  // g_debug ("GetType(%d, %s)", id, name);
  result = mhs_prefs_branch_get_type ((MhsPrefs *)user_data,
                                      id,
                                      name,
                                      type,
                                      &ns_result,
                                      &error);

  if (!result)
    {
      /*g_warning ("Error getting branch type: %s", error->message);*/
      g_error_free (error);
    }

  return ns_result;
}

static guint
clutter_mozheadless_prefs_branch_clear_user (gint         id,
                                             const gchar *name,
                                             gpointer     user_data)
{
  // g_debug ("ClearUser(%d, %s)", id, name);
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

  // g_debug ("HasUserValue(%d, %s)", id, name);
  result = mhs_prefs_branch_has_user_value ((MhsPrefs *)user_data,
                                            id,
                                            name,
                                            has_value,
                                            &ns_result,
                                            &error);

  if (!result)
    {
      g_warning ("Error getting branch has-user-value (%s): %s",
                 name, error->message);
      g_error_free (error);
    }

  return ns_result;
}

static guint
clutter_mozheadless_prefs_branch_delete (gint         id,
                                         const gchar *start,
                                         gpointer     user_data)
{
  // g_debug ("Delete(%d, %s)", id, start);
  return (guint)0;
}

static guint
clutter_mozheadless_prefs_branch_reset (gint         id,
                                        const gchar *start,
                                        gpointer     user_data)
{
  // g_debug ("Reset(%d, %s)", id, start);
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

  // g_debug ("GetChildList(%d, %s)", id, start);
  *len = 0;
  *array = NULL;
  result = mhs_prefs_branch_get_child_list ((MhsPrefs *)user_data,
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

  // g_debug ("Lock(%d, %s)", id, name);
  result = mhs_prefs_branch_lock ((MhsPrefs *)user_data,
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

  // g_debug ("IsLocked(%d, %s)", id, name);
  result = mhs_prefs_branch_is_locked ((MhsPrefs *)user_data,
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

  // g_debug ("Unlock(%d, %s)", id, name);
  result = mhs_prefs_branch_unlock ((MhsPrefs *)user_data,
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

  // g_debug ("GetBool(%d, %s)", id, name);
  result = mhs_prefs_branch_get_bool ((MhsPrefs *)user_data,
                                      id,
                                      name,
                                      value,
                                      &ns_result,
                                      &error);

  if (!result)
    {
      /*g_warning ("Error getting branch boolean value (%s): %s",
                 name, error->message);*/
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

  // g_debug ("SetBool(%d, %s, %d)", id, name, value);
  result = mhs_prefs_branch_set_bool ((MhsPrefs *)user_data,
                                      id,
                                      name,
                                      value,
                                      &ns_result,
                                      &error);

  if (!result)
    {
      g_warning ("Error setting branch boolean value (%s): %s",
                 name, error->message);
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

  // g_debug ("GetChar(%d, %s)", id, name);
  result = mhs_prefs_branch_get_char ((MhsPrefs *)user_data,
                                      id,
                                      name,
                                      value,
                                      &ns_result,
                                      &error);

  if (!result)
    {
      /*g_warning ("Error getting branch char value (%s): %s",
                 name, error->message);*/
      g_error_free (error);
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

  // g_debug ("SetChar(%d, %s, %s)", id, name, value);
  result = mhs_prefs_branch_set_char ((MhsPrefs *)user_data,
                                      id,
                                      name,
                                      value,
                                      &ns_result,
                                      &error);

  if (!result)
    {
      g_warning ("Error setting branch char value (%s): %s",
                 name, error->message);
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

  // g_debug ("GetInt(%d, %s)", id, name);
  result = mhs_prefs_branch_get_int ((MhsPrefs *)user_data,
                                     id,
                                     name,
                                     value,
                                     &ns_result,
                                     &error);

  if (!result)
    {
      /*g_warning ("Error getting branch int value (%s): %s",
                 name, error->message);*/
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

  // g_debug ("SetInt(%d, %s, %d)", id, name, value);
  result = mhs_prefs_branch_set_int ((MhsPrefs *)user_data,
                                     id,
                                     name,
                                     value,
                                     &ns_result,
                                     &error);

  if (!result)
    {
      g_warning ("Error setting branch int value (%s): %s",
                 name, error->message);
      g_error_free (error);
    }

  return ns_result;
}

static guint
clutter_mozheadless_prefs_branch_add_observer (gint         id,
                                               const gchar *domain,
                                               gpointer     user_data)
{
  guint ns_result = NS_ERROR_UNEXPECTED;
  gboolean result;
  GError *error = NULL;

  // g_debug ("AddObserver(%d, %s)", id, domain);
  result = mhs_prefs_branch_add_observer ((MhsPrefs *)user_data,
                                          id,
                                          domain,
                                          &ns_result,
                                          &error);

  if (!result)
    {
      g_warning ("Error adding observer: %s", error->message);
      g_error_free (error);
    }

  return ns_result;
}

static guint
clutter_mozheadless_prefs_branch_remove_observer (gint         id,
                                                  const gchar *domain,
                                                  gpointer     user_data)
{
  guint ns_result = NS_ERROR_UNEXPECTED;
  gboolean result;
  GError *error = NULL;

  // g_debug ("RemoveObserver(%d, %s)", id, domain);
  result = mhs_prefs_branch_remove_observer ((MhsPrefs *)user_data,
                                             id,
                                             domain,
                                             &ns_result,
                                             &error);

  if (!result)
    {
      g_warning ("Error removing observer: %s", error->message);
      g_error_free (error);
    }

  return ns_result;
}

static void
_branch_changed_cb (MhsPrefs *prefs, gint id, const gchar *domain)
{
  // g_debug ("BranchChanged(%d, %s)", id, domain);
  moz_headless_prefs_branch_send_change (id, domain);
}

static MhsPrefs *prefs = NULL;

static void
_init_bool_pref (const gchar *pref, gboolean value)
{
  gboolean result, has_pref;
  guint ns_result;
  GError *error = NULL;

  has_pref = TRUE;
  mhs_prefs_branch_has_user_value (prefs, 0, pref,
                                   &has_pref, &ns_result, &error);
  g_clear_error (&error);

  if (!has_pref)
    {
      result = mhs_prefs_branch_set_bool (prefs, 0,
                                          pref,
                                          value,
                                          &ns_result,
                                          &error);
      if (!result)
        {
          g_warning ("Error initialising preference (%s): %s",
                     pref, error->message);
          g_error_free (error);
        }
    }
}

static void
_init_int_pref (const gchar *pref, gint value)
{
  gboolean result, has_pref;
  guint ns_result;
  GError *error = NULL;

  has_pref = TRUE;
  mhs_prefs_branch_has_user_value (prefs, 0, pref,
                                   &has_pref, &ns_result, &error);
  g_clear_error (&error);

  if (!has_pref)
    {
      result = mhs_prefs_branch_set_int (prefs, 0,
                                         pref,
                                         value,
                                         &ns_result,
                                         &error);
      if (!result)
        {
          g_warning ("Error initialising preference (%s): %s",
                     pref, error->message);
          g_error_free (error);
        }
    }
}

static void
_init_default_prefs ()
{
  /* These preferences need to be set for download dialogs to work */
  /* FIXME: We really ought to just install a default prefs.js... */
  _init_bool_pref ("browser.download.useDownloadDir", TRUE);
  _init_int_pref ("browser.download.folderList", 0);
  _init_bool_pref ("browser.download.manager.showAlertOnComplete", TRUE);
  _init_int_pref ("browser.download.manager.showAlertInterval", 2000);
  _init_int_pref ("browser.download.manager.retention", 2);
  _init_bool_pref ("browser.download.manager.showWhenStarting", TRUE);
  _init_bool_pref ("browser.download.manager.useWindow", TRUE);
  _init_bool_pref ("browser.download.manager.closeWhenDone", TRUE);
  _init_int_pref ("browser.download.manager.openDelay", 0);
  _init_bool_pref ("browser.download.manager.focusWhenStarting", TRUE);
  _init_int_pref ("browser.download.manager.flashCount", 2);
  _init_bool_pref ("browser.download.manager.alertOnEXEOpen", TRUE);

  _init_bool_pref ("browser.dom.window.dump.enabled", TRUE);

  _init_int_pref ("alerts.slideIncrement", 1);
  _init_int_pref ("alerts.slideIncrementTime", 10);
  _init_int_pref ("alerts.totalOpenTime", 4000);
  _init_int_pref ("alerts.height", 50);
}

void
clutter_mozheadless_prefs_init ()
{
  if (!prefs)
    prefs = mhs_prefs_new ();

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

      moz_headless_set_prefs_branch_observer_callbacks (
        clutter_mozheadless_prefs_branch_add_observer,
        clutter_mozheadless_prefs_branch_remove_observer);

      g_signal_connect (prefs, "branch-changed",
                        G_CALLBACK (_branch_changed_cb), NULL);

      _init_default_prefs ();
    }
}

void
clutter_mozheadless_prefs_deinit ()
{
  if (prefs)
    {
      g_signal_handlers_disconnect_by_func (prefs, _branch_changed_cb, NULL);
      g_object_unref (G_OBJECT (prefs));
      prefs = NULL;
    }
}

