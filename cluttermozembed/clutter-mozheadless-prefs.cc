/*
 * ClutterMozHeadless; A headless Mozilla renderer
 * Copyright (c) 2009, Intel Corporation.
 *
 * Portions of this file are Copyright (c) 1998 Netscape
 * Communications Corporation.
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

#include <mhs/mhs.h>
#include <nsIComponentManager.h>
#include <nsIFile.h>
#include <nsIGenericFactory.h>
#include <nsILocalFile.h>
#include <nsIPrefService.h>
#include <nsIPrefBranch2.h>
#include <nsIPrefLocalizedString.h>
#include <nsIObserver.h>
#include <nsIObserverService.h>
#include <nsIProperties.h>
#include <nsIRelativeFilePref.h>
#include <nsISupportsPrimitives.h>
#include <nsCOMPtr.h>
#include <nsMemory.h>
#include <nsServiceManagerUtils.h>
#include <nsWeakReference.h>
#include <nsStringGlue.h>
#include <nsCRTGlue.h>
#include <nsVoidArray.h>

#include "clutter-mozheadless.h"
#include "clutter-mozheadless-prefs.h"

// TODO: Think about splitting this into separate files...

G_BEGIN_DECLS

class HeadlessPrefBranch : public nsIPrefBranch2,
                           public nsIObserver,
                           public nsSupportsWeakReference
{
 public:
  HeadlessPrefBranch(MhsPrefs   *aMhsPrefs,
                     PRUint32    aId,
                     const char *aPrefRoot,
                     PRBool      aDefaultBranch);
  virtual ~HeadlessPrefBranch();

  NS_DECL_ISUPPORTS
  NS_DECL_NSIPREFBRANCH
  NS_DECL_NSIPREFBRANCH2
  NS_DECL_NSIOBSERVER

  PRInt32 GetRootLength() { return mPrefRootLength; }

  void SignalChange(const char *aDomain);

 protected:
  HeadlessPrefBranch() { }
  const char *getPrefName(const char *aPrefName);
  void freeObserverList(void);

 private:
  MhsPrefs            *mMhsPrefs;
  PRUint32             mId;
  PRInt32              mPrefRootLength;
  nsCString            mPrefRoot;
  PRBool               mIsDefault;
  nsAutoVoidArray     *mObservers;
};

class HeadlessPrefService: public nsIPrefService,
                           public nsIPrefBranch2,
                           public nsSupportsWeakReference
{
public:
  HeadlessPrefService();
  virtual ~HeadlessPrefService();

  void ReleaseBranch(gint aId);

  static HeadlessPrefService *GetSingleton(void);

  NS_DECL_ISUPPORTS
  NS_DECL_NSIPREFSERVICE
  NS_FORWARD_NSIPREFBRANCH(mRootBranch->)
  NS_FORWARD_NSIPREFBRANCH2(mRootBranch->)

  HeadlessPrefBranch *GetBranchById(gint aId);

  static HeadlessPrefService *sHeadlessPrefService;

private:
  void     AddBranch(HeadlessPrefBranch *aBranch, gint aId);
  nsresult GetBranch(const char *aPrefRoot, nsIPrefBranch **_retval, PRBool aDefault);

  MhsPrefs                 *mMhsPrefs;
  nsCOMPtr<nsIPrefBranch2>  mRootBranch;
  GHashTable               *mBranchById;
};

G_END_DECLS

// HeadlessPrefService

static void
_branch_changed_cb (MhsPrefs            *prefs,
                    gint                 id,
                    const gchar         *domain,
                    HeadlessPrefService *service)
{
  // g_debug ("BranchChanged(%d, %s)", id, domain);
  HeadlessPrefBranch *branch = service->GetBranchById (id);
  if (branch)
    branch->SignalChange (domain);
}

HeadlessPrefService::HeadlessPrefService(void)
{
  mMhsPrefs = mhs_prefs_new ();
  g_signal_connect (mMhsPrefs, "branch-changed",
                    G_CALLBACK (_branch_changed_cb), this);

  HeadlessPrefBranch *rootBranch =
    new HeadlessPrefBranch (mMhsPrefs, 0, "", PR_FALSE);
  mRootBranch = (nsIPrefBranch2 *)rootBranch;

  mBranchById = g_hash_table_new (g_direct_hash, g_direct_equal);
}

HeadlessPrefService::~HeadlessPrefService()
{
  if (mBranchById)
    {
      g_hash_table_destroy (mBranchById);
      mBranchById = NULL;
    }

  mRootBranch = nsnull;

  if (mMhsPrefs)
    {
      g_signal_handlers_disconnect_by_func (mMhsPrefs,
                                            (gpointer)_branch_changed_cb,
                                            this);
      g_object_unref (mMhsPrefs);
      mMhsPrefs = NULL;
    }

  if (sHeadlessPrefService == this)
    sHeadlessPrefService = nsnull;
}

HeadlessPrefBranch *
HeadlessPrefService::GetBranchById(gint aId)
{
  nsIPrefBranch *retval;

  if (aId == 0) {
    nsresult rv = CallQueryInterface(mRootBranch, &retval);
    if (NS_SUCCEEDED (rv))
      return (HeadlessPrefBranch *)retval;
    else
      return nsnull;
  } else if (aId > 0) {
    return (HeadlessPrefBranch *)g_hash_table_lookup (mBranchById, GINT_TO_POINTER (aId));
  } else {
    return nsnull;
  }
}

HeadlessPrefService *HeadlessPrefService::sHeadlessPrefService = nsnull;

HeadlessPrefService *
HeadlessPrefService::GetSingleton(void)
{
  if (!sHeadlessPrefService) {
    sHeadlessPrefService = new HeadlessPrefService ();
  }

  return sHeadlessPrefService;
}

NS_IMETHODIMP_(nsrefcnt)
HeadlessPrefService::AddRef ()
{
  return 1;
}

NS_IMETHODIMP_(nsrefcnt)
HeadlessPrefService::Release ()
{
  return 1;
}

NS_INTERFACE_MAP_BEGIN(HeadlessPrefService)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIPrefService)
  NS_INTERFACE_MAP_ENTRY(nsIPrefService)
  NS_INTERFACE_MAP_ENTRY(nsIPrefBranch)
  NS_INTERFACE_MAP_ENTRY(nsIPrefBranch2)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
NS_INTERFACE_MAP_END

static char *
_path_from_nsifile (nsIFile *aFile)
{
  char *file;

  if (aFile)
    {
      nsAutoString ns_path;
      aFile->GetPath (ns_path);
      file = ToNewUTF8String (ns_path);
    }
  else
    file = NULL;

  return file;
}

NS_IMETHODIMP
HeadlessPrefService::ReadUserPrefs(nsIFile *aFile)
{
  gboolean result;
  GError *error = NULL;
  guint ns_result = NS_OK;
  char *file = _path_from_nsifile (aFile);

  // g_debug ("ReadUserPrefs(%s)", file);
  result = mhs_prefs_read_user (mMhsPrefs,
                                file,
                                &error);

  NS_Free (file);

  if (!result)
    {
      ns_result = mhs_error_to_nsresult (error);
      g_warning ("Error reading user prefs: %s", error->message);
      g_error_free (error);
    }

  return (nsresult)ns_result;
}

NS_IMETHODIMP
HeadlessPrefService::ResetPrefs()
{
  guint ns_result = NS_OK;
  gboolean result;
  GError *error = NULL;

  // g_debug ("ResetPrefs");
  result = mhs_prefs_reset (mMhsPrefs, &error);

  if (!result)
    {
      ns_result = mhs_error_to_nsresult (error);
      g_warning ("Error resetting prefs: %s", error->message);
      g_error_free (error);
    }

  return (nsresult)ns_result;
}

NS_IMETHODIMP
HeadlessPrefService::ResetUserPrefs()
{
  guint ns_result = NS_OK;
  gboolean result;
  GError *error = NULL;

  // g_debug ("ResetUserPrefs");
  result = mhs_prefs_reset_user (mMhsPrefs,
                                 &error);

  if (!result)
    {
      ns_result = mhs_error_to_nsresult (error);
      g_warning ("Error resetting user prefs: %s", error->message);
      g_error_free (error);
    }

  return (nsresult)ns_result;
}

NS_IMETHODIMP
HeadlessPrefService::SavePrefFile(nsIFile *aFile)
{
  guint ns_result = NS_OK;
  gboolean result;
  GError *error = NULL;
  char *file = _path_from_nsifile (aFile);

  // g_debug ("SavePrefFile(%s)", file);
  result = mhs_prefs_save_pref_file (mMhsPrefs,
                                     file,
                                     &error);

  NS_Free (file);

  if (!result)
    {
      ns_result = mhs_error_to_nsresult (error);
      g_warning ("Error saving prefs file: %s", error->message);
      g_error_free (error);
    }

  return (nsresult)ns_result;
}

nsresult
HeadlessPrefService::GetBranch(const char     *aPrefRoot,
                               nsIPrefBranch **_retval,
                               PRBool          aDefault)
{
  guint ns_result = NS_OK;
  gint id;
  gboolean result;
  GError *error = NULL;

  if (!aDefault && ((aPrefRoot == nsnull) || (*aPrefRoot == '\0')) && (mRootBranch)) {
    return CallQueryInterface(mRootBranch, _retval);
  }

  if (aDefault)
    result = mhs_prefs_get_default_branch (mMhsPrefs,
                                           (const gchar *)aPrefRoot,
                                           &id,
                                           &error);
  else
    result = mhs_prefs_get_branch (mMhsPrefs,
                                   (const gchar *)aPrefRoot,
                                   &id,
                                   &error);

  if (!result)
    {
      ns_result = mhs_error_to_nsresult (error);
      /*g_warning ("Error getting branch: %s", error->message);*/
      g_error_free (error);
    }
  else
    {
      HeadlessPrefBranch *branch = new HeadlessPrefBranch (mMhsPrefs,
                                                           id,
                                                           aPrefRoot,
                                                           aDefault);
      ns_result = (guint)CallQueryInterface(branch, _retval);

      // g_debug ("GotBranch(%s, %d) = %d", aPrefRoot, aDefault, id);
      if (NS_SUCCEEDED (ns_result))
        AddBranch (branch, id);
      else
        NS_RELEASE (branch);
    }

  return (nsresult)ns_result;
}

NS_IMETHODIMP
HeadlessPrefService::GetBranch(const char     *aPrefRoot,
                               nsIPrefBranch **_retval)
{
  return GetBranch(aPrefRoot, _retval, PR_FALSE);
}

NS_IMETHODIMP
HeadlessPrefService::GetDefaultBranch(const char     *aPrefRoot,
                                      nsIPrefBranch **_retval)
{
  return GetBranch(aPrefRoot, _retval, PR_TRUE);
}

void
HeadlessPrefService::AddBranch(HeadlessPrefBranch *aBranch, gint aId)
{
  // Don't insert the root branch
  if (aId > 0)
    g_hash_table_insert (mBranchById,
                         GINT_TO_POINTER (aId),
                         (gpointer)aBranch);
}

void
HeadlessPrefService::ReleaseBranch(gint aId)
{
  gboolean result;
  GError *error = NULL;

  result = mhs_prefs_release_branch (mMhsPrefs,
                                     aId,
                                     &error);

  if (!result)
    {
      g_warning ("Error releasing branch: %s", error->message);
      g_error_free (error);
    }

  if (aId > 0)
    g_hash_table_remove (mBranchById, GINT_TO_POINTER (aId));
}


// HeadlessPrefBranch

HeadlessPrefBranch::HeadlessPrefBranch(MhsPrefs   *aMhsPrefs,
                                       PRUint32    aId,
                                       const char *aPrefRoot,
                                       PRBool      aDefaultBranch)
  : mObservers(nsnull)
{
  mMhsPrefs = aMhsPrefs;
  g_object_add_weak_pointer (G_OBJECT (aMhsPrefs), (gpointer *)&mMhsPrefs);

  mId = aId;
  mPrefRoot = aPrefRoot;
  mPrefRootLength = mPrefRoot.Length();
  mIsDefault = aDefaultBranch;

  nsCOMPtr<nsIObserverService> observerService =
    do_GetService("@mozilla.org/observer-service;1");
  if (observerService) {
    ++mRefCnt;
    observerService->AddObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID, PR_TRUE);
    --mRefCnt;
  }
}

HeadlessPrefBranch::~HeadlessPrefBranch()
{
  freeObserverList();

  if (mMhsPrefs)
    {
      HeadlessPrefService *prefService = HeadlessPrefService::GetSingleton();
      prefService->ReleaseBranch (mId);
    }
}

NS_IMPL_THREADSAFE_ADDREF(HeadlessPrefBranch)
NS_IMPL_THREADSAFE_RELEASE(HeadlessPrefBranch)

NS_INTERFACE_MAP_BEGIN(HeadlessPrefBranch)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIPrefBranch)
  NS_INTERFACE_MAP_ENTRY(nsIPrefBranch)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIPrefBranch2, !mIsDefault)
  NS_INTERFACE_MAP_ENTRY(nsIObserver)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
NS_INTERFACE_MAP_END

NS_IMETHODIMP
HeadlessPrefBranch::GetRoot(char **aRoot)
{
  NS_ENSURE_ARG_POINTER(aRoot);

  mPrefRoot.SetLength(mPrefRootLength);
  *aRoot = ToNewCString(mPrefRoot);

  return NS_OK;
}

NS_IMETHODIMP
HeadlessPrefBranch::GetPrefType(const char *aPrefName, PRInt32 *_retval)
{
  guint ns_result = NS_OK;
  gboolean result;
  GError *error = NULL;
  gint type;

  if (!mMhsPrefs)
    return NS_ERROR_NOT_AVAILABLE;

  // g_debug ("GetType(%d, %s)", id, name);
  result = mhs_prefs_branch_get_type (mMhsPrefs,
                                      mId,
                                      aPrefName,
                                      &type,
                                      &error);

  if (!result)
    {
      ns_result = mhs_error_to_nsresult (error);
      /*g_warning ("Error getting branch type: %s", error->message);*/
      g_error_free (error);
    }
  else
    *_retval = type;

  return (nsresult)ns_result;
}

NS_IMETHODIMP
HeadlessPrefBranch::ClearUserPref(const char *aPrefName)
{
/*  guint ns_result = NS_OK;
  gboolean result;
  GError *error = NULL;

  if (!mMhsPrefs)
    return NS_ERROR_NOT_AVAILABLE;

  // g_debug ("ClearUser(%d, %s)", id, name);
  result = mhs_prefs_branch_clear_user (mMhsPrefs,
                                        mId,
                                        aPrefName,
                                        &error);

  if (!result)
    {
      ns_result = mhs_error_to_nsresult (error);
      g_warning ("Error clearing user pref (%s): %s",
                 aPrefName, error->message);
      g_error_free (error);
    }

  return (nsresult)ns_result;
  */
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HeadlessPrefBranch::PrefHasUserValue(const char *aPrefName, PRBool *_retval)
{
  guint ns_result = NS_OK;
  gboolean result, has_value;
  GError *error = NULL;

  if (!mMhsPrefs)
    return NS_ERROR_NOT_AVAILABLE;

  // g_debug ("HasUserValue(%d, %s)", id, name);
  result = mhs_prefs_branch_has_user_value (mMhsPrefs,
                                            mId,
                                            aPrefName,
                                            &has_value,
                                            &error);

  if (!result)
    {
      ns_result = mhs_error_to_nsresult (error);
      g_warning ("Error getting branch has-user-value (%s): %s",
                 aPrefName, error->message);
      g_error_free (error);
    }
  else
    *_retval = has_value;

  return (nsresult)ns_result;
}

NS_IMETHODIMP
HeadlessPrefBranch::DeleteBranch(const char *aStartingAt)
{
/*  guint ns_result = NS_OK;
  gboolean result;
  GError *error = NULL;

  if (!mMhsPrefs)
    return NS_ERROR_NOT_AVAILABLE;

  // g_debug ("Delete(%d, %s)", id, start);
  result = mhs_prefs_branch_delete (mMhsPrefs,
                                    mId,
                                    aStartingAt,
                                    &error);

  if (!result)
    {
      ns_result = mhs_error_to_nsresult (error);
      g_warning ("Error deleting branch (%s): %s",
                 aStartingAt, error->message);
      g_error_free (error);
    }

  return (nsresult)ns_result;*/
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HeadlessPrefBranch::ResetBranch(const char *aStartingAt)
{
/*  guint ns_result = NS_OK;
  gboolean result;
  GError *error = NULL;

  if (!mMhsPrefs)
    return NS_ERROR_NOT_AVAILABLE;

  // g_debug ("Reset(%d, %s)", id, start);
  result = mhs_prefs_branch_reset (mMhsPrefs,
                                   mId,
                                   aStartingAt,
                                   &error);

  if (!result)
    {
      ns_result = mhs_error_to_nsresult (error);
      g_warning ("Error resetting branch (%s): %s",
                 aStartingAt, error->message);
      g_error_free (error);
    }

  return (nsresult)ns_result;*/
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HeadlessPrefBranch::GetChildList(const char   *aStartingAt,
                                 PRUint32     *aCount,
                                 char       ***aChildArray)
{
  guint ns_result = NS_OK;
  gchar **child_array = NULL;
  GError *error = NULL;
  gboolean result;
  guint count;

  if (!mMhsPrefs)
    return NS_ERROR_NOT_AVAILABLE;

  // g_debug ("GetChildList(%d, %s)", id, start);
  *aCount = 0;
  *aChildArray = nsnull;
  result = mhs_prefs_branch_get_child_list (mMhsPrefs,
                                            mId,
                                            aStartingAt,
                                            &count,
                                            &child_array,
                                            &error);

  if (!result)
    {
      ns_result = mhs_error_to_nsresult (error);
      g_warning ("Error getting branch child list: %s", error->message);
      g_error_free (error);
    }
  else if (child_array && count)
    {
      *aCount = count;
      *aChildArray = (char **)nsMemory::Alloc((*aCount) * sizeof(char *));
      for (PRUint32 i = 0; i < count; i++)
        {
          (*aChildArray)[i] = NS_strdup (child_array[i]);
          g_free (child_array[i]);
        }
      g_free (child_array);
    }

  return (nsresult)ns_result;
}

NS_IMETHODIMP
HeadlessPrefBranch::LockPref(const char *aPrefName)
{
  guint ns_result = NS_OK;
  GError *error = NULL;
  gboolean result;

  if (!mMhsPrefs)
    return NS_ERROR_NOT_AVAILABLE;

  // g_debug ("Lock(%d, %s)", id, name);
  result = mhs_prefs_branch_lock (mMhsPrefs,
                                  mId,
                                  aPrefName,
                                  &error);

  if (!result)
    {
      ns_result = mhs_error_to_nsresult (error);
      g_warning ("Error locking branch: %s", error->message);
      g_error_free (error);
    }

  return (nsresult)ns_result;
}

NS_IMETHODIMP
HeadlessPrefBranch::PrefIsLocked(const char *aPrefName, PRBool *_retval)
{
  guint ns_result = NS_OK;
  gboolean result, value;
  GError *error = NULL;

  if (!mMhsPrefs)
    return NS_ERROR_NOT_AVAILABLE;

  // g_debug ("IsLocked(%d, %s)", id, name);
  result = mhs_prefs_branch_is_locked (mMhsPrefs,
                                       mId,
                                       aPrefName,
                                       &value,
                                       &error);

  if (!result)
    {
      ns_result = mhs_error_to_nsresult (error);
      g_warning ("Error getting branch is-locked state: %s", error->message);
      g_error_free (error);
    }
  else
    *_retval = value;

  return (nsresult)ns_result;
}

NS_IMETHODIMP
HeadlessPrefBranch::UnlockPref(const char *aPrefName)
{
  guint ns_result = NS_OK;
  gboolean result;
  GError *error = NULL;

  if (!mMhsPrefs)
    return NS_ERROR_NOT_AVAILABLE;

  // g_debug ("Unlock(%d, %s)", id, name);
  result = mhs_prefs_branch_unlock (mMhsPrefs,
                                    mId,
                                    aPrefName,
                                    &error);

  if (!result)
    {
      ns_result = mhs_error_to_nsresult (error);
      g_warning ("Error unlocking branch: %s", error->message);
      g_error_free (error);
    }

  return (nsresult)ns_result;
}

NS_IMETHODIMP
HeadlessPrefBranch::GetBoolPref(const char *aPrefName, PRBool *_retval)
{
  guint ns_result = NS_OK;
  gboolean result, value;
  GError *error = NULL;

  if (!mMhsPrefs)
    return NS_ERROR_NOT_AVAILABLE;

  // g_debug ("GetBool(%d, %s)", id, name);
  result = mhs_prefs_branch_get_bool (mMhsPrefs,
                                      mId,
                                      aPrefName,
                                      &value,
                                      &error);

  if (!result)
    {
      ns_result = mhs_error_to_nsresult (error);
      /*g_warning ("Error getting branch boolean value (%s): %s",
                 name, error->message);*/
      g_error_free (error);
    }
  else
    *_retval = value;

  return (nsresult)ns_result;
}

NS_IMETHODIMP
HeadlessPrefBranch::SetBoolPref(const char *aPrefName, PRInt32 aValue)
{
  guint ns_result = NS_OK;
  GError *error = NULL;
  gboolean result;

  if (!mMhsPrefs)
    return NS_ERROR_NOT_AVAILABLE;

  // g_debug ("SetBool(%d, %s, %d)", id, name, value);
  result = mhs_prefs_branch_set_bool (mMhsPrefs,
                                      mId,
                                      aPrefName,
                                      (gboolean)aValue,
                                      &error);

  if (!result)
    {
      ns_result = mhs_error_to_nsresult (error);
      g_warning ("Error setting branch boolean value (%s): %s",
                 aPrefName, error->message);
      g_error_free (error);
    }

  return (nsresult)ns_result;
}

NS_IMETHODIMP
HeadlessPrefBranch::GetCharPref(const char *aPrefName, char **_retval)
{
  guint ns_result = NS_OK;
  GError *error = NULL;
  gboolean result;
  gchar *value;

  if (!mMhsPrefs)
    return NS_ERROR_NOT_AVAILABLE;

  // g_debug ("GetChar(%d, %s)", id, name);
  result = mhs_prefs_branch_get_char (mMhsPrefs,
                                      mId,
                                      aPrefName,
                                      &value,
                                      &error);

  if (!result)
    {
      ns_result = mhs_error_to_nsresult (error);
      /*g_warning ("Error getting branch char value (%s): %s",
                 name, error->message);*/
      g_error_free (error);
    }
  else
    {
      *_retval = value ? NS_strdup (value) : nsnull;
      g_free (value);
    }

  return (nsresult)ns_result;
}

NS_IMETHODIMP
HeadlessPrefBranch::SetCharPref(const char *aPrefName, const char *aValue)
{
  guint ns_result = NS_OK;
  GError *error = NULL;
  gboolean result;

  if (!mMhsPrefs)
    return NS_ERROR_NOT_AVAILABLE;

  // g_debug ("SetChar(%d, %s, %s)", id, name, value);
  result = mhs_prefs_branch_set_char (mMhsPrefs,
                                      mId,
                                      aPrefName,
                                      (const gchar *)aValue,
                                      &error);

  if (!result)
    {
      ns_result = mhs_error_to_nsresult (error);
      g_warning ("Error setting branch char value (%s): %s",
                 aPrefName, error->message);
      g_error_free (error);
    }

  return (nsresult)ns_result;
}

NS_IMETHODIMP
HeadlessPrefBranch::GetIntPref(const char *aPrefName, PRInt32 *_retval)
{
  guint ns_result = NS_OK;
  GError *error = NULL;
  gboolean result;
  gint value;

  if (!mMhsPrefs)
    return NS_ERROR_NOT_AVAILABLE;

  // g_debug ("GetInt(%d, %s)", mId, aPrefName);
  result = mhs_prefs_branch_get_int (mMhsPrefs,
                                     mId,
                                     aPrefName,
                                     &value,
                                     &error);

  if (!result)
    {
      ns_result = mhs_error_to_nsresult (error);
      /*g_warning ("Error getting branch int value (%s): %s",
                 name, error->message);*/
      g_error_free (error);
    }
  else
    *_retval = value;

  return (nsresult)ns_result;
}

NS_IMETHODIMP
HeadlessPrefBranch::SetIntPref(const char *aPrefName, PRInt32 aValue)
{
  guint ns_result = NS_OK;
  gboolean result;
  GError *error = NULL;

  if (!mMhsPrefs)
    return NS_ERROR_NOT_AVAILABLE;

  // g_debug ("SetInt(%d, %s, %d)", id, name, value);
  result = mhs_prefs_branch_set_int (mMhsPrefs,
                                     mId,
                                     aPrefName,
                                     (gint)aValue,
                                     &error);

  if (!result)
    {
      ns_result = mhs_error_to_nsresult (error);
      g_warning ("Error setting branch int value (%s): %s",
                 aPrefName, error->message);
      g_error_free (error);
    }

  return (nsresult)ns_result;
}

// Get/SetComplexValue copied from nsPrefBranch.cpp with slight modifications
// for readability and for it to work
NS_IMETHODIMP
HeadlessPrefBranch::GetComplexValue(const char *aPrefName,
                                    const nsIID &aType,
                                    void **_retval)
{
  nsresult rv;
  nsCString utf8String;

  if (!mMhsPrefs)
    return NS_ERROR_NOT_AVAILABLE;

  // we have to do this one first because it's different than all the rest
  if (aType.Equals(NS_GET_IID(nsIPrefLocalizedString))) {
    nsCOMPtr<nsIPrefLocalizedString> theString(
        do_CreateInstance(NS_PREFLOCALIZEDSTRING_CONTRACTID, &rv));

    if (NS_SUCCEEDED(rv)) {
      PRBool bNeedDefault = PR_FALSE;

      if (mIsDefault) {
        bNeedDefault = PR_TRUE;
      } else {
        // if there is no user (or locked) value
        PRBool hasUserValue, isLocked;

        rv = PrefHasUserValue(aPrefName, &hasUserValue);
        if (NS_FAILED(rv))
          return rv;

        rv = PrefIsLocked(aPrefName, &isLocked);
        if (NS_FAILED(rv))
          return rv;

        if (!hasUserValue && !isLocked) {
          bNeedDefault = PR_TRUE;
        }
      }

      // if we need to fetch the default value, do that instead, otherwise use the
      // value we pulled in at the top of this function
      if (bNeedDefault != mIsDefault) {
        // FIXME: Can't access default properties from non-default branch
        return NS_ERROR_NOT_IMPLEMENTED;
      } else {
        rv = GetCharPref(aPrefName, getter_Copies(utf8String));
        if (NS_SUCCEEDED(rv)) {
          rv = theString->SetData(NS_ConvertUTF8toUTF16(utf8String).get());
        }
      }
      if (NS_SUCCEEDED(rv)) {
        nsIPrefLocalizedString *temp = theString;

        NS_ADDREF(temp);
        *_retval = (void *)temp;
      }
    }

    return rv;
  }

  // Try to fall back to GetCharPref
  rv = GetCharPref(aPrefName, getter_Copies(utf8String));
  if (NS_FAILED(rv))
    return rv;

  if (aType.Equals(NS_GET_IID(nsILocalFile))) {
    nsCOMPtr<nsILocalFile> file(do_CreateInstance(NS_LOCAL_FILE_CONTRACTID, &rv));

    if (NS_SUCCEEDED(rv)) {
      rv = file->SetPersistentDescriptor(utf8String);
      if (NS_SUCCEEDED(rv)) {
        nsILocalFile *temp = file;

        NS_ADDREF(temp);
        *_retval = (void *)temp;
        return NS_OK;
      }
    }
    return rv;
  }

  if (aType.Equals(NS_GET_IID(nsIRelativeFilePref))) {
    // FIXME: I've not checked that this code works
    // (previously it was using ::const_iterator)
    const char *keyBegin, *strEnd;
    utf8String.BeginReading(&keyBegin);
    strEnd = utf8String.EndReading();

    // The pref has the format: [fromKey]a/b/c
    if (*keyBegin++ != '[')
        return NS_ERROR_FAILURE;

    const char *keyEnd = keyBegin;
    if (!(keyEnd = g_utf8_strchr(keyEnd, strEnd - keyBegin, ']')))
      return NS_ERROR_FAILURE;

    nsCAutoString key(Substring(keyBegin, keyEnd));

    nsCOMPtr<nsILocalFile> fromFile;
    nsCOMPtr<nsIProperties> directoryService(
        do_GetService(NS_DIRECTORY_SERVICE_CONTRACTID, &rv));
    if (NS_FAILED(rv))
      return rv;

    rv = directoryService->Get(key.get(),
                               NS_GET_IID(nsILocalFile),
                               getter_AddRefs(fromFile));
    if (NS_FAILED(rv))
      return rv;

    nsCOMPtr<nsILocalFile> theFile;
    rv = NS_NewNativeLocalFile(EmptyCString(), PR_TRUE, getter_AddRefs(theFile));
    if (NS_FAILED(rv))
      return rv;

    rv = theFile->SetRelativeDescriptor(fromFile, Substring(++keyEnd, strEnd));
    if (NS_FAILED(rv))
      return rv;

    nsCOMPtr<nsIRelativeFilePref> relativePref;
    rv = NS_NewRelativeFilePref(theFile, key, getter_AddRefs(relativePref));
    if (NS_FAILED(rv))
      return rv;

    *_retval = relativePref;
    NS_ADDREF(static_cast<nsIRelativeFilePref*>(*_retval));

    return NS_OK;
  }

  if (aType.Equals(NS_GET_IID(nsISupportsString))) {
    nsCOMPtr<nsISupportsString> theString(
        do_CreateInstance(NS_SUPPORTS_STRING_CONTRACTID, &rv));

    if (NS_SUCCEEDED(rv)) {
      rv = theString->SetData(NS_ConvertUTF8toUTF16(utf8String));
      if (NS_SUCCEEDED(rv)) {
        nsISupportsString *temp = theString;

        NS_ADDREF(temp);
        *_retval = (void *)temp;
        return NS_OK;
      }
    }
    return rv;
  }

  NS_WARNING("HeadlessPrefBranch::GetComplexValue - Unsupported interface type");
  return NS_NOINTERFACE;
}

NS_IMETHODIMP
HeadlessPrefBranch::SetComplexValue(const char  *aPrefName,
                                    const nsIID &aType,
                                    nsISupports *aValue)
{
  nsresult   rv = NS_NOINTERFACE;

  if (!mMhsPrefs)
    return NS_ERROR_NOT_AVAILABLE;

  if (aType.Equals(NS_GET_IID(nsILocalFile))) {
    nsCOMPtr<nsILocalFile> file = do_QueryInterface(aValue);
    if (!file)
      return NS_NOINTERFACE;
    nsCAutoString descriptorString;

    rv = file->GetPersistentDescriptor(descriptorString);
    if (NS_SUCCEEDED(rv)) {
      rv = SetCharPref(aPrefName, descriptorString.get());
    }
    return rv;
  }

  if (aType.Equals(NS_GET_IID(nsIRelativeFilePref))) {
    nsCOMPtr<nsIRelativeFilePref> relFilePref = do_QueryInterface(aValue);
    if (!relFilePref)
      return NS_NOINTERFACE;

    nsCOMPtr<nsILocalFile> file;
    relFilePref->GetFile(getter_AddRefs(file));
    if (!file)
      return NS_NOINTERFACE;
    nsCAutoString relativeToKey;
    (void) relFilePref->GetRelativeToKey(relativeToKey);

    nsCOMPtr<nsILocalFile> relativeToFile;
    nsCOMPtr<nsIProperties> directoryService(do_GetService(NS_DIRECTORY_SERVICE_CONTRACTID, &rv));
    if (NS_FAILED(rv))
      return rv;
    rv = directoryService->Get(relativeToKey.get(), NS_GET_IID(nsILocalFile), getter_AddRefs(relativeToFile));
    if (NS_FAILED(rv))
      return rv;

    nsCAutoString relDescriptor;
    rv = file->GetRelativeDescriptor(relativeToFile, relDescriptor);
    if (NS_FAILED(rv))
      return rv;

    nsCAutoString descriptorString;
    descriptorString.Append('[');
    descriptorString.Append(relativeToKey);
    descriptorString.Append(']');
    descriptorString.Append(relDescriptor);
    return SetCharPref(aPrefName, descriptorString.get());
  }

  if (aType.Equals(NS_GET_IID(nsISupportsString))) {
    nsCOMPtr<nsISupportsString> theString = do_QueryInterface(aValue);

    if (theString) {
      nsAutoString wideString;

      rv = theString->GetData(wideString);
      if (NS_SUCCEEDED(rv)) {
        rv = SetCharPref(aPrefName, NS_ConvertUTF16toUTF8(wideString).get());
      }
    }
    return rv;
  }

  if (aType.Equals(NS_GET_IID(nsIPrefLocalizedString))) {
    nsCOMPtr<nsIPrefLocalizedString> theString = do_QueryInterface(aValue);

    if (theString) {
      nsString wideString;

      rv = theString->GetData(getter_Copies(wideString));
      if (NS_SUCCEEDED(rv)) {
        rv = SetCharPref(aPrefName, NS_ConvertUTF16toUTF8(wideString).get());
      }
    }
    return rv;
  }

  NS_WARNING("HeadlessPrefBranch::SetComplexValue - Unsupported interface type");
  return NS_NOINTERFACE;
}

typedef struct {
  char             *pDomain;
  nsIObserver      *pObserver;
  nsIWeakReference *pWeakRef;
} PrefCallbackData;

NS_IMETHODIMP
HeadlessPrefBranch::AddObserver(const char  *aDomain,
                                nsIObserver *aObserver,
                                PRBool       aHoldWeak)
{
  PrefCallbackData *pCallback;

  NS_ENSURE_ARG_POINTER(aDomain);
  NS_ENSURE_ARG_POINTER(aObserver);

  if (!mMhsPrefs)
    return NS_ERROR_NOT_AVAILABLE;

  if (!mObservers) {
    mObservers = new nsAutoVoidArray();
    if (!mObservers)
      return NS_ERROR_OUT_OF_MEMORY;
  }

  pCallback = (PrefCallbackData *)nsMemory::Alloc(sizeof(PrefCallbackData));
  if (!pCallback)
    return NS_ERROR_OUT_OF_MEMORY;

  pCallback->pObserver = aObserver;

  if (aHoldWeak) {
    nsCOMPtr<nsISupportsWeakReference> weakRefFactory = do_QueryInterface(aObserver);
    if (!weakRefFactory) {
      nsMemory::Free (pCallback);
      return NS_ERROR_INVALID_ARG;
    }
    nsCOMPtr<nsIWeakReference> tmp = do_GetWeakReference(weakRefFactory);
    NS_ADDREF(pCallback->pWeakRef = tmp);
  } else {
    pCallback->pWeakRef = nsnull;
    NS_ADDREF(pCallback->pObserver);
  }

  pCallback->pDomain = NS_strdup (aDomain);
  if (!pCallback->pDomain) {
    nsMemory::Free (pCallback);
    return NS_ERROR_OUT_OF_MEMORY;
  }

  mObservers->AppendElement(pCallback);

  guint ns_result = NS_OK;
  GError *error = NULL;
  gboolean result;

  // g_debug ("AddObserver(%d, %s, %d) (%p)", mId, aDomain, aHoldWeak, (void *)this);
  result = mhs_prefs_branch_add_observer (mMhsPrefs,
                                          mId,
                                          (const gchar *)aDomain,
                                          &error);

  if (!result)
    {
      ns_result = mhs_error_to_nsresult (error);
      g_warning ("Error adding observer: %s", error->message);
      g_error_free (error);
    }

  return (nsresult)ns_result;
}

NS_IMETHODIMP
HeadlessPrefBranch::RemoveObserver(const char *aDomain, nsIObserver *aObserver)
{
  PrefCallbackData *pCallback;
  PRInt32 count;
  PRInt32 i;

  NS_ENSURE_ARG_POINTER(aDomain);
  NS_ENSURE_ARG_POINTER(aObserver);

  if (!mMhsPrefs)
    return NS_ERROR_NOT_AVAILABLE;

  if (!mObservers)
    return NS_OK;

  count = mObservers->Count();
  if (count == 0)
    return NS_OK;

  for (i = 0; i < count; i++) {
    pCallback = (PrefCallbackData *)mObservers->ElementAt(i);
    if (pCallback && (strcmp (pCallback->pDomain, aDomain) == 0)) {
      break;
    }
    pCallback = NULL;
  }

  guint ns_result = NS_OK;

  if (pCallback) {
    gboolean result;
    GError *error = NULL;

    // g_debug ("RemoveObserver(%d, %s)", mId, aDomain);
    // This needs to be called first, as aDomain == pDomain, possibly
    result = mhs_prefs_branch_remove_observer (mMhsPrefs,
                                               mId,
                                               (const gchar *)aDomain,
                                               &error);

    if (!result)
      {
        ns_result = mhs_error_to_nsresult (error);
        g_warning ("Error removing observer: %s", error->message);
        g_error_free (error);
      }

    mObservers->RemoveElementAt(i);
    NS_Free (pCallback->pDomain);

    if (pCallback->pWeakRef) {
      NS_RELEASE(pCallback->pWeakRef);
    } else {
      NS_RELEASE(pCallback->pObserver);
    }

    nsMemory::Free(pCallback);
  }

  return (nsresult)ns_result;
}

NS_IMETHODIMP
HeadlessPrefBranch::Observe(nsISupports     *aSubject,
                            const char      *aTopic,
                            const PRUnichar *someData)
{
  // watch for xpcom shutdown and free our observers to eliminate any cyclic references
  if (!strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID)) {
    freeObserverList();
  }
  return NS_OK;
}

void
HeadlessPrefBranch::SignalChange(const char *aDomain)
{
  PrefCallbackData *pCallback;
  PRInt32 count;
  PRInt32 i;

  if (!aDomain)
    return;

  if (!mObservers)
    return;

  count = mObservers->Count();
  if (count == 0)
    return;

  // g_debug ("SignalChange: %d, %s", mId, aDomain);
  for (i = 0; i < count; i++) {
    pCallback = (PrefCallbackData *)mObservers->ElementAt(i);
    if (pCallback &&
        g_str_has_prefix (aDomain, pCallback->pDomain)) {
      pCallback->pObserver->Observe (static_cast<nsIPrefBranch *>(this),
                                 NS_PREFBRANCH_PREFCHANGE_TOPIC_ID,
                                 NS_ConvertUTF8toUTF16 (aDomain).get());
    }
  }
}

void HeadlessPrefBranch::freeObserverList(void)
{
  PrefCallbackData *pCallback;

  if (!mObservers)
    return;

  while (mObservers->Count() > 0) {
    pCallback = (PrefCallbackData *)mObservers->ElementAt(0);
    RemoveObserver (pCallback->pDomain, pCallback->pObserver);
  }

  delete mObservers;
  mObservers = 0;
}

NS_GENERIC_FACTORY_SINGLETON_CONSTRUCTOR(HeadlessPrefService, HeadlessPrefService::GetSingleton)

static const nsModuleComponentInfo prefServiceComp = {
  "Preferences Service",
  NS_PREFSERVICE_CID,
  NS_PREFSERVICE_CONTRACTID,
  HeadlessPrefServiceConstructor
};

void
clutter_mozheadless_prefs_init ()
{
  static gboolean comp_is_registered = FALSE;

  if (!comp_is_registered)
    {
      moz_headless_register_component ((gpointer)&prefServiceComp);
      comp_is_registered = TRUE;
    }
}

void
clutter_mozheadless_prefs_deinit ()
{
  if (HeadlessPrefService::sHeadlessPrefService)
    delete HeadlessPrefService::sHeadlessPrefService;
}

