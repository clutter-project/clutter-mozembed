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
 * Authored by Neil Roberts <neil@linux.intel.com>
 */

#include <mhs/mhs.h>
#include <nsIPermissionManager.h>
#include <nsIPermission.h>
#include <nsServiceManagerUtils.h>
#include <nsComponentManagerUtils.h>
#include <nsStringAPI.h>
#include <nsIURI.h>
#include <nsISimpleEnumerator.h>
#include <nsCOMPtr.h>
#include <nsIGenericFactory.h>
#include <nscore.h>
#include <nsCOMArray.h>
#include <nsArrayEnumerator.h>

#include "clutter-mozheadless.h"
#include "clutter-mozheadless-permission-manager.h"

class HeadlessPermissionManager : public nsIPermissionManager
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIPERMISSIONMANAGER

  HeadlessPermissionManager ();

  static HeadlessPermissionManager *sHeadlessPermissionManager;

  static HeadlessPermissionManager *GetSingleton (void);

  ~HeadlessPermissionManager ();

private:
  MhsPermissionManager *mMhsPm;
};

class HeadlessPermission : public nsIPermission
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIPERMISSION

  HeadlessPermission (const gchar *host_arg,
                      const gchar *type_arg,
                      PRUint32     aCapability,
                      PRUint32     aExpireType,
                      PRInt64      aExpireTime);
  virtual ~HeadlessPermission ();

protected:
  nsCString host;
  nsCString type;
  PRUint32  capability;
  PRUint32  expire_type;
  PRInt64   expire_time;
};

// {a1a81d35-1dad-4ad1-b989-0c6bd476072e}
#define HEADLESS_PMSERVICE_CID \
  { 0xa1a81d35, 0x1dad, 0x4ad1, \
      { 0xb9, 0x89, 0x0c, 0x6b, 0xd4, 0x76, 0x07, 0x2e } }

NS_GENERIC_FACTORY_SINGLETON_CONSTRUCTOR
(HeadlessPermissionManager, HeadlessPermissionManager::GetSingleton)

static const nsModuleComponentInfo pmServiceComp = {
  "Permission Manager  Service",
  HEADLESS_PMSERVICE_CID,
  NS_PERMISSIONMANAGER_CONTRACTID,
  HeadlessPermissionManagerConstructor
};

HeadlessPermissionManager *
HeadlessPermissionManager::sHeadlessPermissionManager = nsnull;

HeadlessPermissionManager *
HeadlessPermissionManager::GetSingleton (void)
{
  if (!sHeadlessPermissionManager)
    sHeadlessPermissionManager = new HeadlessPermissionManager ();

  return sHeadlessPermissionManager;
}

NS_IMETHODIMP_(nsrefcnt)
HeadlessPermissionManager::AddRef ()
{
  return 1;
}

NS_IMETHODIMP_(nsrefcnt)
HeadlessPermissionManager::Release ()
{
  return 1;
}

NS_INTERFACE_MAP_BEGIN(HeadlessPermissionManager)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIPermissionManager)
  NS_INTERFACE_MAP_ENTRY(nsIPermissionManager)
NS_INTERFACE_MAP_END

HeadlessPermissionManager::HeadlessPermissionManager ()
{
  mMhsPm = mhs_permission_manager_new ();
}

HeadlessPermissionManager::~HeadlessPermissionManager ()
{
  g_object_unref (mMhsPm);

  if (sHeadlessPermissionManager == this)
    sHeadlessPermissionManager = nsnull;
}

/* void add (in nsIURI uri, in string type, in PRUint32 permission); */
NS_IMETHODIMP
HeadlessPermissionManager::Add (nsIURI *uri,
                                const char *type,
                                PRUint32 permission,
                                PRUint32 expireType,
                                PRInt64  expireTime)
{
  nsresult rv = NS_OK;
  GError *error = NULL;
  nsCAutoString spec;

  rv = uri->GetSpec (spec);
  NS_ENSURE_SUCCESS (rv, rv);

  if (!mhs_pm_add (mMhsPm,
                   spec.get (),
                   type,
                   permission,
                   expireType,
                   expireTime,
                   &error))
    {
      rv = mhs_error_to_nsresult (error);
      g_error_free (error);
    }

  return rv;
}

/* void remove (in AUTF8String host, in string type); */
NS_IMETHODIMP
HeadlessPermissionManager::Remove (const nsACString &host, const char *type)
{
  nsresult rv = NS_OK;
  GError *error = NULL;

  if (!mhs_pm_remove (mMhsPm,
                      PromiseFlatCString (host).get (),
                      type,
                      &error))
    {
      rv = mhs_error_to_nsresult (error);
      g_error_free (error);
    }

  return rv;
}

/* void removeAll (); */
NS_IMETHODIMP
HeadlessPermissionManager::RemoveAll ()
{
  nsresult rv = NS_OK;
  GError *error = NULL;

  if (!mhs_pm_remove_all (mMhsPm, &error))
    {
      rv = mhs_error_to_nsresult (error);
      g_error_free (error);
    }

  return rv;
}

/* PRUint32 testPermission (in nsIURI uri, in string type); */
NS_IMETHODIMP
HeadlessPermissionManager::TestPermission (nsIURI *uri,
                                           const char *type,
                                           PRUint32 *ret NS_OUTPARAM)
{
  nsresult rv = NS_OK;
  GError *error = NULL;
  nsCAutoString spec;

  rv = uri->GetSpec (spec);
  NS_ENSURE_SUCCESS (rv, rv);

  if (!mhs_pm_test_permission (mMhsPm,
                               spec.get (),
                               type,
                               ret,
                               &error))
    {
      rv = mhs_error_to_nsresult (error);
      g_error_free (error);
    }

  return rv;
}

/* PRUint32 testExactPermission (in nsIURI uri, in string type); */
NS_IMETHODIMP
HeadlessPermissionManager::TestExactPermission (nsIURI *uri,
                                                const char *type,
                                                PRUint32 *ret NS_OUTPARAM)
{
  nsresult rv = NS_OK;
  GError *error = NULL;
  nsCAutoString spec;

  rv = uri->GetSpec (spec);
  NS_ENSURE_SUCCESS (rv, rv);

  if (!mhs_pm_test_exact_permission (mMhsPm,
                                     spec.get (),
                                     type,
                                     ret,
                                     &error))
    {
      rv = mhs_error_to_nsresult (error);
      g_error_free (error);
    }

  return rv;
}

/* readonly attribute nsISimpleEnumerator enumerator; */
NS_IMETHODIMP
HeadlessPermissionManager::GetEnumerator (nsISimpleEnumerator **enumerator)
{
  nsresult rv = NS_OK;
  GError *error = NULL;

  guint32 n_perms;
  MhsPermission *perms;

  if (!mhs_pm_get_all (mMhsPm,
                       &n_perms,
                       &perms,
                       &error))
    {
      rv = mhs_error_to_nsresult (error);
      g_error_free (error);
    }
  else
    {
      nsCOMArray<nsIPermission> perm_array (n_perms);

      for (guint32 i = 0; i < n_perms; i++)
        {
          HeadlessPermission *hp =
            new HeadlessPermission (perms[i].host,
                                    perms[i].type,
                                    perms[i].capability,
                                    perms[i].expire_type,
                                    perms[i].expire_time);
          perm_array.AppendObject (hp);
        }

      rv = NS_NewArrayEnumerator (enumerator, perm_array);

      mhs_pm_free_permissions (n_perms, perms);
    }

  return rv;
}

NS_IMPL_ISUPPORTS1 (HeadlessPermission, nsIPermission)

HeadlessPermission::HeadlessPermission (const gchar *host_arg,
                                        const gchar *type_arg,
                                        PRUint32     capability_arg,
                                        PRUint32     expire_type_arg,
                                        PRInt64      expire_time_arg)
: host (host_arg),
  type (type_arg),
  capability (capability_arg),
  expire_type (expire_type_arg),
  expire_time (expire_time_arg)
{
}

HeadlessPermission::~HeadlessPermission ()
{
}

NS_IMETHODIMP
HeadlessPermission::GetHost (nsACString &host_out)
{
  host_out = host;
  return NS_OK;
}

NS_IMETHODIMP
HeadlessPermission::GetType (nsACString &type_out)
{
  type_out = type;
  return NS_OK;
}

NS_IMETHODIMP
HeadlessPermission::GetCapability (PRUint32 *capability_out)
{
  *capability_out = capability;
  return NS_OK;
}

NS_IMETHODIMP
HeadlessPermission::GetExpireType (PRUint32 *expire_type_out)
{
  *expire_type_out = expire_type;
  return NS_OK;
}

NS_IMETHODIMP
HeadlessPermission::GetExpireTime (PRInt64 *expire_time_out)
{
  *expire_time_out = expire_time;
  return NS_OK;
}

void
clutter_mozheadless_permission_manager_init ()
{
  static gboolean comp_is_registered = FALSE;

  if (!comp_is_registered)
    {
      moz_headless_register_component ((gpointer) &pmServiceComp);
      comp_is_registered = TRUE;
    }
}

void
clutter_mozheadless_permission_manager_deinit ()
{
  if (HeadlessPermissionManager::sHeadlessPermissionManager)
    delete HeadlessPermissionManager::sHeadlessPermissionManager;
}
