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
#include <nsIComponentManager.h>
#include <nsIGenericFactory.h>
#include <nsILoginManagerStorage.h>
#include <nsILoginManager.h>
#include <nsILoginInfo.h>
#include <nsICategoryManager.h>
#include <nsServiceManagerUtils.h>
#include <nsComponentManagerUtils.h>
#include <nsStringAPI.h>
#include <nsIPropertyBag.h>
#include <nsIProperty.h>
#include <nsISimpleEnumerator.h>
#include <nsIVariant.h>
#include <nsCOMPtr.h>
#include <nsMemory.h>
#include <nsCRTGlue.h>

#include "clutter-mozheadless.h"
#include "clutter-mozheadless-login-manager-storage.h"

class HeadlessLoginManagerStorage : public nsILoginManagerStorage
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSILOGINMANAGERSTORAGE

  HeadlessLoginManagerStorage ();

  static HeadlessLoginManagerStorage *sHeadlessLoginManagerStorage;

  static HeadlessLoginManagerStorage *GetSingleton (void);

  ~HeadlessLoginManagerStorage ();

private:

  nsresult ConvertMhsLoginInfos (PRUint32 *count_out,
                                 nsILoginInfo ***logins_out,
                                 guint n_logins,
                                 MhsLoginInfo *logins);
  nsresult ConvertPropertyBagToHashTable (nsIPropertyBag *properties,
                                          GHashTable *hash_table);

  MhsLoginManagerStorage *mMhsLms;
};

// {55ae85e6-b08c-4421-8785-02f454fb69ef}
#define HEADLESS_LMSSERVICE_CID \
  { 0x55ae85e6, 0xb08c, 0x4421, \
      { 0x87, 0x85, 0x02, 0xf4, 0x54, 0xfb, 0x69, 0xef } }

#define HEADLESS_LMS_CONTRACTID \
  "@clutter-project.org/login-manager-storage-service;1"

NS_GENERIC_FACTORY_SINGLETON_CONSTRUCTOR
(HeadlessLoginManagerStorage, HeadlessLoginManagerStorage::GetSingleton)

static const nsModuleComponentInfo lmsServiceComp = {
  "Login Manager Storage Service",
  HEADLESS_LMSSERVICE_CID,
  HEADLESS_LMS_CONTRACTID,
  HeadlessLoginManagerStorageConstructor
};

/* Helper class which wraps an nsCString but has a method to return a
   NULL pointer if the string is void */
class HeadlessStringValue
{
public:
  HeadlessStringValue () { }

  /* Convert from UTF-16 string */
  explicit HeadlessStringValue (const nsAString &other)
  {
    *this = other;
  }

  HeadlessStringValue &operator= (const nsAString &other)
  {
    if (other.IsVoid ())
      value.SetIsVoid (true);
    else
      {
        value.SetIsVoid (false);
        NS_UTF16ToCString (other, NS_CSTRING_ENCODING_UTF8, value);
      }

    return *this;
  }

  const gchar *GetOrNull () { return value.IsVoid () ? NULL : value.get (); }

private:
  nsCString value;
};

/* Helper class to aid converting from nsILoginInfo to MhsLoginInfo */
class HeadlessLoginInfo : public MhsLoginInfo
{
public:
  HeadlessLoginInfo (nsILoginInfo *ns_login_info);
  nsresult rv;

private:
  /* These are stored as members so that the MhsLoginInfo fields can
     point to their data directly. This avoids an extra g_strdup */
  HeadlessStringValue hostname_str;
  HeadlessStringValue form_submit_url_str;
  HeadlessStringValue http_realm_str;
  HeadlessStringValue username_str;
  HeadlessStringValue password_str;
  HeadlessStringValue username_field_str;
  HeadlessStringValue password_field_str;
};

HeadlessLoginManagerStorage *
HeadlessLoginManagerStorage::sHeadlessLoginManagerStorage = nsnull;

HeadlessLoginManagerStorage *
HeadlessLoginManagerStorage::GetSingleton (void)
{
  if (!sHeadlessLoginManagerStorage)
    sHeadlessLoginManagerStorage = new HeadlessLoginManagerStorage ();

  return sHeadlessLoginManagerStorage;
}

NS_IMETHODIMP_(nsrefcnt)
HeadlessLoginManagerStorage::AddRef ()
{
  return 1;
}

NS_IMETHODIMP_(nsrefcnt)
HeadlessLoginManagerStorage::Release ()
{
  return 1;
}

NS_INTERFACE_MAP_BEGIN(HeadlessLoginManagerStorage)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsILoginManagerStorage)
  NS_INTERFACE_MAP_ENTRY(nsILoginManagerStorage)
NS_INTERFACE_MAP_END

HeadlessLoginManagerStorage::HeadlessLoginManagerStorage ()
{
  mMhsLms = mhs_login_manager_storage_new ();
}

HeadlessLoginManagerStorage::~HeadlessLoginManagerStorage ()
{
  g_object_unref (mMhsLms);

  if (sHeadlessLoginManagerStorage == this)
    sHeadlessLoginManagerStorage = nsnull;
}

/* void init (); */
NS_IMETHODIMP
HeadlessLoginManagerStorage::Init ()
{
  nsresult rv = NS_OK;
  GError *error = NULL;

  if (!mhs_lms_init (mMhsLms, &error))
    {
      rv = mhs_error_to_nsresult (error);
      g_warning ("Error initializing the login manager storage: %s",
                 error->message);
      g_error_free (error);
    }

  return rv;
}

/* void initWithFile (in nsIFile aInputFile, in nsIFile aOutputFile); */
NS_IMETHODIMP
HeadlessLoginManagerStorage::InitWithFile (nsIFile *aInputFile,
                                           nsIFile *aOutputFile)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

/* void addLogin (in nsILoginInfo aLogin); */
NS_IMETHODIMP
HeadlessLoginManagerStorage::AddLogin (nsILoginInfo *aLogin)
{
  nsresult rv = NS_OK;
  GError *error = NULL;
  HeadlessLoginInfo login_info (aLogin);

  if (NS_FAILED (login_info.rv))
    return login_info.rv;

  if (!mhs_lms_add_login (mMhsLms,
                          login_info.hostname,
                          login_info.form_submit_url,
                          login_info.http_realm,
                          login_info.username,
                          login_info.password,
                          login_info.username_field,
                          login_info.password_field,
                          &error))
    {
      rv = mhs_error_to_nsresult (error);
      g_warning ("Error adding a login: %s",
                 error->message);
      g_error_free (error);
    }

  return rv;
}

/* void removeLogin (in nsILoginInfo aLogin); */
NS_IMETHODIMP
HeadlessLoginManagerStorage::RemoveLogin (nsILoginInfo *aLogin)
{
  nsresult rv = NS_OK;
  GError *error = NULL;
  HeadlessLoginInfo login_info (aLogin);

  if (NS_FAILED (login_info.rv))
    return login_info.rv;

  if (!mhs_lms_remove_login (mMhsLms, &login_info, &error))
    {
      rv = mhs_error_to_nsresult (error);
      g_warning ("Error removing a login: %s",
                 error->message);
      g_error_free (error);
    }

  return rv;
}

/* void modifyLogin (in nsILoginInfo oldLogin, in nsISupports newLoginData); */
NS_IMETHODIMP
HeadlessLoginManagerStorage::ModifyLogin (nsILoginInfo *oldLogin,
                                          nsISupports *newLoginData)
{
  nsresult rv = NS_OK;
  GError *error = NULL;
  HeadlessLoginInfo login_info (oldLogin);
  GHashTable *new_values;

  if (NS_FAILED (login_info.rv))
    return login_info.rv;

  /* Extract all of the properties of the property bag into a
     GHashTable */
  new_values = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

  nsCOMPtr<nsILoginInfo> login_info_data =
    do_QueryInterface (newLoginData, &rv);
  if (NS_SUCCEEDED (rv))
    {
      HeadlessLoginInfo new_login_info (login_info_data);

      if (NS_FAILED (new_login_info.rv))
        rv = new_login_info.rv;
      else
        {
          if (new_login_info.hostname)
            g_hash_table_insert (new_values,
                                 g_strdup ("hostname"),
                                 g_strdup (new_login_info.hostname));
          if (new_login_info.http_realm)
            g_hash_table_insert (new_values,
                                 g_strdup ("httpRealm"),
                                 g_strdup (new_login_info.http_realm));
          if (new_login_info.form_submit_url)
            g_hash_table_insert (new_values,
                                 g_strdup ("formSubmitURL"),
                                 g_strdup (new_login_info.form_submit_url));
          if (new_login_info.username)
            g_hash_table_insert (new_values,
                                 g_strdup ("username"),
                                 g_strdup (new_login_info.username));
          if (new_login_info.password)
            g_hash_table_insert (new_values,
                                 g_strdup ("password"),
                                 g_strdup (new_login_info.password));
          if (new_login_info.username_field)
            g_hash_table_insert (new_values,
                                 g_strdup ("usernameField"),
                                 g_strdup (new_login_info.username_field));
          if (new_login_info.password_field)
            g_hash_table_insert (new_values,
                                 g_strdup ("passwordField"),
                                 g_strdup (new_login_info.password_field));
        }
    }
  else
    {
      nsCOMPtr<nsIPropertyBag> property_bag =
        do_QueryInterface (newLoginData, &rv);
      if (NS_SUCCEEDED (rv))
        rv = ConvertPropertyBagToHashTable (property_bag, new_values);
    }

  if (rv == NS_OK &&
      !mhs_lms_modify_login (mMhsLms,
                             &login_info,
                             new_values,
                             &error))
    {
      rv = mhs_error_to_nsresult (error);
      g_warning ("Error modifying a login: %s",
                 error->message);
      g_error_free (error);
    }

  g_hash_table_unref (new_values);

  return rv;
}

/* void removeAllLogins (); */
NS_IMETHODIMP
HeadlessLoginManagerStorage::RemoveAllLogins ()
{
  nsresult rv = NS_OK;
  GError *error = NULL;

  if (!mhs_lms_remove_all_logins (mMhsLms, &error))
    {
      rv = mhs_error_to_nsresult (error);
      g_warning ("Error removing all logins: %s",
                 error->message);
      g_error_free (error);
    }

  return rv;
}

/* void getAllLogins (out unsigned long count,
                      [array, retval, size_is (count)]
                      out nsILoginInfo logins); */
NS_IMETHODIMP
HeadlessLoginManagerStorage::GetAllLogins (PRUint32 *count_out NS_OUTPARAM,
                                           nsILoginInfo ***logins_out
                                           NS_OUTPARAM)
{
  nsresult rv = NS_OK;
  GError *error = NULL;
  guint n_logins;
  MhsLoginInfo *logins;

  if (mhs_lms_get_all_logins (mMhsLms, &n_logins, &logins, &error))
    rv = ConvertMhsLoginInfos (count_out, logins_out,
                               n_logins, logins);
  else
    {
      rv = mhs_error_to_nsresult (error);
      g_warning ("Error getting all logins: %s",
                 error->message);
      g_error_free (error);
    }

  return rv;
}

/* void getAllEncryptedLogins (out unsigned long count,
                               [array, retval, size_is (count)]
                               out nsILoginInfo logins); */
NS_IMETHODIMP
HeadlessLoginManagerStorage::GetAllEncryptedLogins (PRUint32 *count_out
                                                    NS_OUTPARAM,
                                                    nsILoginInfo ***logins_out
                                                    NS_OUTPARAM)
{
  nsresult rv = NS_OK;
  GError *error = NULL;
  guint n_logins;
  MhsLoginInfo *logins;

  if (mhs_lms_get_all_encrypted_logins (mMhsLms, &n_logins, &logins, &error))
    {
      guint n_logins;
      MhsLoginInfo *logins;

      rv = ConvertMhsLoginInfos (count_out, logins_out,
                                 n_logins, logins);
    }
  else
    {
      rv = mhs_error_to_nsresult (error);
      g_warning ("Error getting all logins: %s",
                 error->message);
      g_error_free (error);
    }

  return rv;
}

/* void searchLogins (out unsigned long count,
                      in nsIPropertyBag matchData,
                      [array, retval, size_is (count)]
                      out nsILoginInfo logins); */
NS_IMETHODIMP
HeadlessLoginManagerStorage::SearchLogins (PRUint32 *count_out NS_OUTPARAM,
                                           nsIPropertyBag *matchData,
                                           nsILoginInfo ***logins_out
                                           NS_OUTPARAM)
{
  nsresult rv = NS_OK;
  GError *error = NULL;
  guint n_logins;
  MhsLoginInfo *logins;
  GHashTable *hash_table;
  nsCOMPtr<nsISimpleEnumerator> enumerator;

  hash_table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

  rv = ConvertPropertyBagToHashTable (matchData, hash_table);
  if (NS_SUCCEEDED (rv))
    {
      if (mhs_lms_search_logins (mMhsLms,
                                 hash_table,
                                 &n_logins, &logins,
                                 &error))
        {
          guint n_logins;
          MhsLoginInfo *logins;

          rv = ConvertMhsLoginInfos (count_out, logins_out,
                                     n_logins, logins);
        }
      else
        {
          rv = mhs_error_to_nsresult (error);
          g_error_free (error);
        }
    }

  g_hash_table_unref (hash_table);

  return rv;
}

/* void getAllDisabledHosts (out unsigned long count,
                             [array, retval, size_is (count)]
                             out wstring hostnames); */
NS_IMETHODIMP
HeadlessLoginManagerStorage::GetAllDisabledHosts (PRUint32 *count NS_OUTPARAM,
                                                  PRUnichar ***hostnames
                                                  NS_OUTPARAM)
{
  nsresult rv = NS_OK;
  GError *error = NULL;
  gchar **hostnames_strv;

  if (mhs_lms_get_all_disabled_hosts (mMhsLms, &hostnames_strv, &error))
    {
      *count = g_strv_length (hostnames_strv);
      *hostnames = (PRUnichar **) nsMemory::Alloc (*count *
                                                   sizeof (PRUnichar *));
      if (*hostnames == NULL)
        rv = NS_ERROR_OUT_OF_MEMORY;
      else
        {
          guint i;

          memset (*hostnames, 0, *count * sizeof (PRUnichar *));

          for (i = 0; i < *count; i++)
            {
              NS_ConvertUTF8toUTF16 str (hostnames_strv[i]);
              (*hostnames)[i] = NS_strdup (str.get ());
              if ((* hostnames[i]) == NULL)
                rv = NS_ERROR_OUT_OF_MEMORY;
            }

          if (NS_FAILED (rv))
            {
              for (i = 0; i < *count && (*hostnames)[i]; i++)
                nsMemory::Free ((*hostnames)[i]);
              nsMemory::Free (*hostnames);
            }
        }

      g_strfreev (hostnames_strv);
    }
  else
    {
      rv = mhs_error_to_nsresult (error);
      g_warning ("Error getting all disabled hosts: %s",
                 error->message);
      g_error_free (error);
    }

  return rv;
}

/* boolean getLoginSavingEnabled (in AString aHost); */
NS_IMETHODIMP
HeadlessLoginManagerStorage::GetLoginSavingEnabled (const nsAString &aHost,
                                                    PRBool *retval NS_OUTPARAM)
{
  nsresult rv = NS_OK;
  GError *error = NULL;
  gboolean is_enabled;

  if (mhs_lms_get_login_saving_enabled (mMhsLms,
                                        NS_ConvertUTF16toUTF8 (aHost).get (),
                                        &is_enabled,
                                        &error))
    *retval = is_enabled;
  else
    {
      rv = mhs_error_to_nsresult (error);
      g_warning ("Error getting logging saving enabled: %s",
                 error->message);
      g_error_free (error);
    }

  return rv;
}

/* void setLoginSavingEnabled (in AString aHost, in boolean isEnabled); */
NS_IMETHODIMP
HeadlessLoginManagerStorage::SetLoginSavingEnabled (const nsAString &aHost,
                                                    PRBool isEnabled)
{
  nsresult rv = NS_OK;
  GError *error = NULL;

  if (!mhs_lms_set_login_saving_enabled (mMhsLms,
                                         NS_ConvertUTF16toUTF8 (aHost).get (),
                                         isEnabled,
                                         &error))
    {
      rv = mhs_error_to_nsresult (error);
      g_warning ("Error setting logging saving enabled: %s",
                 error->message);
      g_error_free (error);
    }

  return rv;
}

/* void findLogins (out unsigned long count,
                    in AString aHostname,
                    in AString aActionURL,
                    in AString aHttpRealm,
                    [array, retval, size_is (count)]
                    out nsILoginInfo logins); */
NS_IMETHODIMP
HeadlessLoginManagerStorage::FindLogins (PRUint32 *count_out NS_OUTPARAM,
                                         const nsAString &aHostname,
                                         const nsAString &aActionURL,
                                         const nsAString &aHttpRealm,
                                         nsILoginInfo ***logins_out NS_OUTPARAM)
{
  nsresult rv = NS_OK;
  GError *error = NULL;
  guint n_logins;
  MhsLoginInfo *logins;

  if (mhs_lms_find_logins (mMhsLms,
                           HeadlessStringValue (aHostname).GetOrNull (),
                           HeadlessStringValue (aActionURL).GetOrNull (),
                           HeadlessStringValue (aHttpRealm).GetOrNull (),
                           &n_logins, &logins,
                           &error))
    rv = ConvertMhsLoginInfos (count_out, logins_out,
                               n_logins, logins);
  else
    {
      rv = mhs_error_to_nsresult (error);
      g_error_free (error);
    }

  return rv;
}

/* unsigned long countLogins (in AString aHostname,
                              in AString aActionURL,
                              in AString aHttpRealm); */
NS_IMETHODIMP
HeadlessLoginManagerStorage::CountLogins (const nsAString &aHostname,
                                          const nsAString &aActionURL,
                                          const nsAString &aHttpRealm,
                                          PRUint32 *retval NS_OUTPARAM)
{
  nsresult rv = NS_OK;
  GError *error = NULL;
  guint n_logins;

  if (mhs_lms_count_logins (mMhsLms,
                            HeadlessStringValue (aHostname).GetOrNull (),
                            HeadlessStringValue (aActionURL).GetOrNull (),
                            HeadlessStringValue (aHttpRealm).GetOrNull (),
                            &n_logins,
                            &error))
    *retval = n_logins;
  else
    {
      rv = mhs_error_to_nsresult (error);
      g_warning ("Error counting logins: %s",
                 error->message);
      g_error_free (error);
    }

  return rv;
}

nsresult typedef (nsILoginInfo::*HeadlessLoginInfoSetter) (const nsAString &);

static HeadlessLoginInfoSetter loginInfoSetters[] =
  {
    &nsILoginInfo::SetHostname,
    &nsILoginInfo::SetFormSubmitURL,
    &nsILoginInfo::SetHttpRealm,
    &nsILoginInfo::SetUsername,
    &nsILoginInfo::SetPassword,
    &nsILoginInfo::SetUsernameField,
    &nsILoginInfo::SetPasswordField
  };

nsresult
HeadlessLoginManagerStorage::ConvertMhsLoginInfos (PRUint32 *count_out,
                                                   nsILoginInfo ***logins_out,
                                                   guint n_logins,
                                                   MhsLoginInfo *logins)
{
  // This takes ownership of the logins array

  nsresult rv = NS_OK;

  *count_out = n_logins;
  *logins_out = (nsILoginInfo **) nsMemory::Alloc (n_logins *
                                                   sizeof (nsILoginInfo *));
  if (*logins_out == NULL)
    rv = NS_ERROR_OUT_OF_MEMORY;
  else
    {
      memset (*logins_out, 0, n_logins * sizeof (nsILoginInfo *));

      for (guint i = 0; i < n_logins; i++)
        {
          gchar **loginFields = (gchar **) (logins + i);
          guint fieldNum;

          nsCOMPtr<nsILoginInfo> loginInfo
            = do_CreateInstance ("@mozilla.org/login-manager/loginInfo;1", &rv);
          if (NS_FAILED (rv))
            break;
          (* logins_out)[i] = loginInfo;
          NS_ADDREF (loginInfo);

          // Set each field of the nsILoginInfo
          for (fieldNum = 0;
               fieldNum < G_N_ELEMENTS (loginInfoSetters);
               fieldNum++)
            if (loginFields[fieldNum])
              {
                rv = ((loginInfo->*loginInfoSetters[fieldNum])
                      (NS_ConvertUTF8toUTF16 (loginFields[fieldNum])));
                if (NS_FAILED (rv))
                  break;
              }
          if (fieldNum < G_N_ELEMENTS (loginInfoSetters))
            break;
        }
    }

  mhs_lms_free_login_infos (n_logins, logins);

  // If the conversion failed then we need to clean up the work in
  // progress
  if (NS_FAILED (rv) && *logins_out)
    {
      for (guint i = 0; i < n_logins && (*logins_out)[i]; i++)
        NS_RELEASE ((*logins_out)[i]);
      NS_Free (*logins_out);
    }

  return rv;
}

nsresult
HeadlessLoginManagerStorage::ConvertPropertyBagToHashTable (nsIPropertyBag *
                                                            property_bag,
                                                            GHashTable *
                                                            hash_table)
{
  nsresult rv;

  nsCOMPtr<nsISimpleEnumerator> enumerator;
  rv = property_bag->GetEnumerator (getter_AddRefs (enumerator));

  if (NS_SUCCEEDED (rv))
    {
      PRBool hasMoreElements;

      while (true)
        {
          rv = enumerator->HasMoreElements (&hasMoreElements);
          if (NS_FAILED (rv) || !hasMoreElements)
            break;
          nsCOMPtr<nsIProperty> property;
          rv = enumerator->GetNext (getter_AddRefs (property));
          if (NS_FAILED (rv))
            break;
          nsAutoString name;
          rv = property->GetName (name);
          if (NS_FAILED (rv))
            break;
          nsCOMPtr<nsIVariant> variant_value;
          rv = property->GetValue (getter_AddRefs (variant_value));
          if (NS_FAILED (rv))
            break;
          nsAutoString value;
          rv = variant_value->GetAsAString (value);
          if (NS_FAILED (rv))
            break;

          g_hash_table_insert (hash_table,
                               g_strdup (NS_ConvertUTF16toUTF8 (name)
                                         .get ()),
                               g_strdup (NS_ConvertUTF16toUTF8 (value)
                                         .get ()));
        }
    }

  return rv;
}

void
clutter_mozheadless_login_manager_storage_init ()
{
  static gboolean comp_is_registered = FALSE;

  if (!comp_is_registered)
    {
      nsresult rv;

      moz_headless_register_component ((gpointer) &lmsServiceComp);
      comp_is_registered = TRUE;

      /* Register the service with the category manager so that the
         login manager will use it instead of the default service */
      nsCOMPtr<nsICategoryManager> categoryManager =
        do_GetService ("@mozilla.org/categorymanager;1", &rv);
      if (NS_FAILED (rv))
        g_warning ("Error getting category manager service: 0x%x", rv);
      else
        {
          char *oldValue;

          rv = categoryManager->
            AddCategoryEntry ("login-manager-storage",
                              "nsILoginManagerStorage",
                              HEADLESS_LMS_CONTRACTID,
                              PR_FALSE,
                              PR_TRUE,
                              &oldValue);
          if (NS_FAILED (rv))
            g_warning ("Error registering the login manager storage service "
                       "with the category manager: 0x%x", rv);
          else
            {
              if (oldValue)
                nsMemory::Free (oldValue);

              /* Ensure login manager is up and running */
              nsCOMPtr<nsILoginManager> loginManager =
                do_GetService ("@mozilla.org/login-manager;1", &rv);
              if (NS_FAILED (rv))
                g_warning ("Error starting login manager: 0x%x", rv);
            }
        }
    }
}

void
clutter_mozheadless_login_manager_storage_deinit ()
{
  if (HeadlessLoginManagerStorage::sHeadlessLoginManagerStorage)
    delete HeadlessLoginManagerStorage::sHeadlessLoginManagerStorage;
}

HeadlessLoginInfo::HeadlessLoginInfo (nsILoginInfo *ns_login_info)
{
  nsAutoString val;

  if (NS_SUCCEEDED (rv = ns_login_info->GetHostname (val)))
    {
      hostname_str = val;
      hostname = (gchar *) hostname_str.GetOrNull ();
    }
  else
    goto fail;
  if (NS_SUCCEEDED (rv = ns_login_info->GetFormSubmitURL (val)))
    {
      form_submit_url_str = val;
      form_submit_url = (gchar *) form_submit_url_str.GetOrNull ();
    }
  else
    goto fail;
  if (NS_SUCCEEDED (rv = ns_login_info->GetHttpRealm (val)))
    {
      http_realm_str = val;
      http_realm = (gchar *) http_realm_str.GetOrNull ();
    }
  else
    goto fail;
  if (NS_SUCCEEDED (rv = ns_login_info->GetUsername (val)))
    {
      username_str = val;
      username = (gchar *) username_str.GetOrNull ();
    }
  else
    goto fail;
  if (NS_SUCCEEDED (rv = ns_login_info->GetPassword (val)))
    {
      password_str = val;
      password = (gchar *) password_str.GetOrNull ();
    }
  else
    goto fail;
  if (NS_SUCCEEDED (rv = ns_login_info->GetUsernameField (val)))
    {
      username_field_str = val;
      username_field = (gchar *) username_field_str.GetOrNull ();
    }
  else
    goto fail;
  if (NS_SUCCEEDED (rv = ns_login_info->GetPasswordField (val)))
    {
      password_field_str = val;
      password_field = (gchar *) password_field_str.GetOrNull ();
    }
  else
    goto fail;

  return;

 fail:
  memset (static_cast<MhsLoginInfo *> (this), 0, sizeof (MhsLoginInfo));
}
