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

#include <nsIComponentManager.h>
#include <nsICookieService.h>
#include <nsICookieManager.h>
#include <nsICookieManager2.h>
#include <nsICookie.h>
#include <nsICookie2.h>
#include <nsIFile.h>
#include <nsIGenericFactory.h>
#include <nsILocalFile.h>
#include <nsIObserver.h>
#include <nsIObserverService.h>
#include <nsISupportsPrimitives.h>
#include <nsIURI.h>
#include <nsArrayEnumerator.h>
#include <nsCOMArray.h>
#include <nsCOMPtr.h>
#include <nsMemory.h>
#include <nsNetCID.h>
#include <nsServiceManagerUtils.h>
#include <nsWeakReference.h>
#include <nsStringGlue.h>
#include <nsNetUtil.h>
#include <nsCRTGlue.h>
#include <mhs/mhs.h>

#include "clutter-mozheadless.h"
#include "clutter-mozheadless-cookies.h"

G_BEGIN_DECLS

class HeadlessCookie : public nsICookie2
{
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSICOOKIE
  NS_DECL_NSICOOKIE2

  virtual ~HeadlessCookie()
    {
      g_free (mName);
      g_free (mValue);
      g_free (mHost);
      g_free (mPath);
    }

  HeadlessCookie(gchar   *aName,
                 gchar   *aValue,
                 gchar   *aHost,
                 gchar   *aPath,
                 PRInt64  aExpiry,
                 PRInt64  aCreationTime,
                 PRBool   aIsSession,
                 PRBool   aIsSecure,
                 PRBool   aIsHttpOnly)
    : mName (aName)
    , mValue (aValue)
    , mHost (aHost)
    , mPath (aPath)
    , mExpiry (aExpiry)
    , mCreationTime (aCreationTime)
    , mIsSession (aIsSession != PR_FALSE)
    , mIsSecure (aIsSecure != PR_FALSE)
    , mIsHttpOnly (aIsHttpOnly != PR_FALSE)
    {
    }

 private:
  gchar        *mName;
  gchar        *mValue;
  gchar        *mHost;
  gchar        *mPath;
  PRInt64       mExpiry;
  PRInt64       mCreationTime;
  PRPackedBool  mIsSession;
  PRPackedBool  mIsSecure;
  PRPackedBool  mIsHttpOnly;
};


class HeadlessCookieService : public nsICookieService,
                              public nsICookieManager2,
                              public nsSupportsWeakReference
{
 public:
  HeadlessCookieService();
  virtual ~HeadlessCookieService();

  static HeadlessCookieService *GetSingleton(void);

  NS_DECL_ISUPPORTS
  NS_DECL_NSICOOKIESERVICE
  NS_DECL_NSICOOKIEMANAGER
  NS_DECL_NSICOOKIEMANAGER2

  static HeadlessCookieService *sHeadlessCookieService;

private:
  MhsCookies                   *mMhsCookies;
};

G_END_DECLS

NS_IMPL_ISUPPORTS2(HeadlessCookie,
                   nsICookie2,
                   nsICookie)

//////////////////////////////
// nsICookie implementation //
//////////////////////////////

NS_IMETHODIMP
HeadlessCookie::GetName (nsACString &aName)
{
  aName = mName;
  return NS_OK;
}

NS_IMETHODIMP
HeadlessCookie::GetValue (nsACString &aValue)
{
  aValue = mValue;
  return NS_OK;
}

NS_IMETHODIMP
HeadlessCookie::GetIsDomain (PRBool *aIsDomain)
{
  *aIsDomain = (*mHost == '.');
  return NS_OK;
}

NS_IMETHODIMP
HeadlessCookie::GetHost (nsACString &aHost)
{
  aHost = mHost;
  return NS_OK;
}

NS_IMETHODIMP
HeadlessCookie::GetPath (nsACString &aPath)
{
  aPath = mPath;
  return NS_OK;
}

NS_IMETHODIMP
HeadlessCookie::GetIsSecure (PRBool *aIsSecure)
{
  *aIsSecure = mIsSecure;
  return NS_OK;
}

NS_IMETHODIMP
HeadlessCookie::GetExpires (PRUint64 *aExpires)
{
  *aExpires = mExpiry;
  return NS_OK;
}

// Deprecated function
NS_IMETHODIMP
HeadlessCookie::GetStatus (nsCookieStatus *aStatus)
{
  *aStatus = STATUS_UNKNOWN;
  return NS_OK;
}

// Deprecated function
NS_IMETHODIMP
HeadlessCookie::GetPolicy (nsCookiePolicy *aPolicy)
{
  *aPolicy = POLICY_UNKNOWN;
  return NS_OK;
}

///////////////////////////////
// nsICookie2 implementation //
///////////////////////////////

NS_IMETHODIMP
HeadlessCookie::GetRawHost (nsACString &aRawHost)
{
  if (*mHost == '.')
    aRawHost = nsDependentCString (mHost + 1);
  else
    aRawHost = nsDependentCString (mHost, (PRUint32)((mPath - 1) - mHost));

  return NS_OK;
}

NS_IMETHODIMP
HeadlessCookie::GetIsSession (PRBool *aIsSession)
{
  *aIsSession = mIsSession;
  return NS_OK;
}

NS_IMETHODIMP
HeadlessCookie::GetExpiry (PRInt64 *aExpiry)
{
  *aExpiry = mExpiry;
  return NS_OK;
}

NS_IMETHODIMP
HeadlessCookie::GetIsHttpOnly (PRBool *aIsHttpOnly)
{
  *aIsHttpOnly = mIsHttpOnly;
  return NS_OK;
}

NS_IMETHODIMP
HeadlessCookie::GetCreationTime (PRInt64 *aCreationTime)
{
  *aCreationTime = mCreationTime;
  return NS_OK;
}

///////////////////////////
// HeadlessCookieService //
///////////////////////////

HeadlessCookieService::HeadlessCookieService(void)
{
  mMhsCookies = mhs_cookies_new ();
}

HeadlessCookieService::~HeadlessCookieService()
{
  if (mMhsCookies) {
    g_object_unref (mMhsCookies);
    mMhsCookies = NULL;
  }

  if (sHeadlessCookieService == this)
    sHeadlessCookieService = nsnull;
}

HeadlessCookieService *HeadlessCookieService::sHeadlessCookieService = nsnull;

HeadlessCookieService *
HeadlessCookieService::GetSingleton(void)
{
  if (!sHeadlessCookieService) {
    sHeadlessCookieService = new HeadlessCookieService;
  }

  return sHeadlessCookieService;
}

NS_IMETHODIMP_(nsrefcnt)
HeadlessCookieService::AddRef ()
{
  return 1;
}

NS_IMETHODIMP_(nsrefcnt)
HeadlessCookieService::Release ()
{
  return 1;
}

NS_INTERFACE_MAP_BEGIN(HeadlessCookieService)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsICookieService)
  NS_INTERFACE_MAP_ENTRY(nsICookieService)
  NS_INTERFACE_MAP_ENTRY(nsICookieManager)
  NS_INTERFACE_MAP_ENTRY(nsICookieManager2)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
NS_INTERFACE_MAP_END


/////////////////////////////////////
// nsICookieService implementation //
/////////////////////////////////////

NS_IMETHODIMP
HeadlessCookieService::GetCookieString (nsIURI      *aURI,
                                        nsIChannel  *aChannel,
                                        char       **_retval NS_OUTPARAM)
{
  gboolean result;
  nsCAutoString uri;
  GError *error = NULL;
  guint ns_result = NS_OK;

  nsresult rv = aURI->GetSpec (uri);
  if (NS_FAILED (rv))
    return rv;

  gchar *cookie = NULL;
  result = mhs_cookies_get_cookie_string (mMhsCookies,
                                          uri.get (),
                                          &cookie,
                                          &error);

  if (!result) {
    ns_result = mhs_error_to_nsresult (error);
    g_warning ("Error getting cookie string: %s", error->message);
    g_error_free (error);
  } else {
    *_retval = cookie ? NS_strdup (cookie) : nsnull;
    g_free (cookie);
  }

  return (nsresult)ns_result;
}

NS_IMETHODIMP
HeadlessCookieService::GetCookieStringFromHttp (nsIURI      *aURI,
                                                nsIURI      *aFirstURI,
                                                nsIChannel  *aChannel,
                                                char       **_retval NS_OUTPARAM)
{
  gboolean result;
  nsCAutoString uri, first_uri;
  GError *error = NULL;
  guint ns_result = NS_OK;

  nsresult rv = aURI->GetSpec (uri);
  if (NS_FAILED (rv))
    return rv;

  rv = aFirstURI->GetSpec (first_uri);
  if (NS_FAILED (rv))
    return rv;

  gchar *cookie = NULL;
  result = mhs_cookies_get_cookie_string_from_http (mMhsCookies,
                                                    uri.get (),
                                                    first_uri.get (),
                                                    &cookie,
                                                    &error);

  if (!result) {
    ns_result = mhs_error_to_nsresult (error);
    g_warning ("Error getting cookie string from http: %s", error->message);
    g_error_free (error);
  } else {
    *_retval = cookie ? NS_strdup (cookie) : nsnull;
    g_free (cookie);
  }

  return (nsresult)ns_result;
}

NS_IMETHODIMP
HeadlessCookieService::SetCookieString (nsIURI     *aURI,
                                        nsIPrompt  *aPrompt,
                                        const char *aCookie,
                                        nsIChannel *aChannel)
{
  gboolean result;
  nsCAutoString uri;
  GError *error = NULL;
  guint ns_result = NS_OK;

  nsresult rv = aURI->GetSpec (uri);
  if (NS_FAILED (rv))
    return rv;

  result = mhs_cookies_set_cookie_string (mMhsCookies,
                                          uri.get (),
                                          aCookie,
                                          &error);

  if (!result) {
    ns_result = mhs_error_to_nsresult (error);
    g_warning ("Error setting cookie string: %s", error->message);
    g_error_free (error);
  }

  return (nsresult)ns_result;
}

NS_IMETHODIMP
HeadlessCookieService::SetCookieStringFromHttp (nsIURI     *aURI,
                                                nsIURI     *aFirstURI,
                                                nsIPrompt  *aPrompt,
                                                const char *aCookie,
                                                const char *aServerTime,
                                                nsIChannel *aChannel)
{
  gboolean result;
  nsCAutoString uri, first_uri;
  GError *error = NULL;
  guint ns_result = NS_OK;

  nsresult rv = aURI->GetSpec (uri);
  if (NS_FAILED (rv))
    return rv;

  rv = aFirstURI->GetSpec (first_uri);
  if (NS_FAILED (rv))
    return rv;

  result = mhs_cookies_set_cookie_string_from_http (mMhsCookies,
                                                    uri.get (),
                                                    first_uri.get (),
                                                    aCookie,
                                                    aServerTime,
                                                    &error);

  if (!result) {
    ns_result = mhs_error_to_nsresult (error);
    g_warning ("Error setting cookie string from http: %s", error->message);
    g_error_free (error);
  }

  return (nsresult)ns_result;
}

/////////////////////////////////////
// nsICookieManager implementation //
/////////////////////////////////////

NS_IMETHODIMP
HeadlessCookieService::RemoveAll ()
{
  gboolean result;
  GError *error = NULL;
  guint ns_result = NS_OK;

  result = mhs_cookies_remove_all (mMhsCookies,
                                   &error);

  if (!result) {
    ns_result = mhs_error_to_nsresult (error);
    g_warning ("Error removing all cookies: %s", error->message);
    g_error_free (error);
  }

  return (nsresult)ns_result;
}

NS_IMETHODIMP
HeadlessCookieService::GetEnumerator (nsISimpleEnumerator **aEnumerator)
{
  gboolean result;
  GError *error = NULL;
  guint ns_result = NS_OK;
  GPtrArray *ptr_array = NULL;

  result = mhs_cookies_get_all (mMhsCookies,
                                &ptr_array,
                                &error);

  if (!result) {
    ns_result = mhs_error_to_nsresult (error);
    g_warning ("Error getting all cookies: %s", error->message);
    g_error_free (error);
  } else if (ptr_array) {
    nsCOMArray<nsICookie> cookies (ptr_array->len);

    for (guint i = 0; i < ptr_array->len; i++) {
      gchar *name, *value, *host, *path;
      gint64 expiry, creation;
      gboolean is_session, is_secure, is_http_only;

      GValueArray *array = (GValueArray *)g_ptr_array_index (ptr_array, i);

      // Initialise default values (TODO: Verify these are sensible,
      // although they shouldn't ever get used)
      name = value = host = path = NULL;
      expiry = creation = 0;
      is_session = TRUE;
      is_secure = is_http_only = FALSE;

      for (guint j = 0; j < array->n_values; j++) {
        GValue *gvalue = g_value_array_get_nth (array, j);

        switch (j) {
          case 0:
            name = g_value_dup_string (gvalue);
            break;

          case 1:
            value = g_value_dup_string (gvalue);
            break;

          case 2:
            host = g_value_dup_string (gvalue);
            break;

          case 3:
            path = g_value_dup_string (gvalue);
            break;

          case 4:
            expiry = g_value_get_int64 (gvalue);
            break;

          case 5:
            creation = g_value_get_int64 (gvalue);
            break;

          case 6:
            is_session = g_value_get_boolean (gvalue);
            break;

          case 7:
            is_secure = g_value_get_boolean (gvalue);
            break;

          case 8:
            is_http_only = g_value_get_boolean (gvalue);
            break;

          default:
            g_warning ("Unexpected value in cookie, ignoring");
            break;
        }
      }

      HeadlessCookie *cookie = new HeadlessCookie (name,
                                                   value,
                                                   host,
                                                   path,
                                                   expiry,
                                                   creation,
                                                   is_session,
                                                   is_secure,
                                                   is_http_only);
      cookies.AppendObject (static_cast<nsICookie *>(cookie));

      g_value_array_free (array);
    }

    g_ptr_array_free (ptr_array, TRUE);
    ns_result = NS_NewArrayEnumerator (aEnumerator, cookies);
  } else
    *aEnumerator = nsnull;

  return (nsresult)ns_result;
}

NS_IMETHODIMP
HeadlessCookieService::Remove (const nsACString &aDomain,
                               const nsACString &aName,
                               const nsACString &aPath,
                               PRBool            aBlocked)
{
  gboolean result;
  GError *error = NULL;
  guint ns_result = NS_OK;

  result = mhs_cookies_remove (mMhsCookies,
                               nsCString (aDomain).get (),
                               nsCString (aName).get (),
                               nsCString (aPath).get (),
                               aBlocked,
                               &error);

  if (!result) {
    ns_result = mhs_error_to_nsresult (error);
    g_warning ("Error removing cookie: %s", error->message);
    g_error_free (error);
  }

  return (nsresult)ns_result;
}

//////////////////////////////////////
// nsICookieManager2 implementation //
//////////////////////////////////////

NS_IMETHODIMP
HeadlessCookieService::Add (const nsACString &aDomain,
                            const nsACString &aPath,
                            const nsACString &aName,
                            const nsACString &aValue,
                            PRBool            aIsSecure,
                            PRBool            aIsHttpOnly,
                            PRBool            aIsSession,
                            PRInt64           aExpiry)
{
  gboolean result;
  GError *error = NULL;
  guint ns_result = NS_OK;

  result = mhs_cookies_add (mMhsCookies,
                            nsCString (aDomain).get (),
                            nsCString (aPath).get (),
                            nsCString (aName).get (),
                            nsCString (aValue).get (),
                            aIsSecure,
                            aIsHttpOnly,
                            aIsSession,
                            aExpiry,
                            &error);

  if (!result) {
    ns_result = mhs_error_to_nsresult (error);
    g_warning ("Error adding cookie: %s", error->message);
    g_error_free (error);
  }

  return (nsresult)ns_result;
}

NS_IMETHODIMP
HeadlessCookieService::CookieExists (nsICookie2 *aCookie,
                                     PRBool     *_retval NS_OUTPARAM)
{
  nsCAutoString host, name, path;
  NS_ENSURE_ARG_POINTER (aCookie);

  // FIXME: This function is extremely slow, we should think of either
  // adding it to the service (which would still be slow, but a lot
  // less so), or keeping a local cache of cookies.

  nsresult rv = aCookie->GetHost (host);
  if (NS_FAILED (rv))
    return rv;

  rv = aCookie->GetName (name);
  if (NS_FAILED (rv))
    return rv;

  rv = aCookie->GetPath (path);
  if (NS_FAILED (rv))
    return rv;

  nsISimpleEnumerator *enumerator;
  rv = GetEnumerator (&enumerator);
  if (NS_FAILED (rv))
    return rv;

  PRBool has_more;
  PRInt64 now = PR_Now () / PR_USEC_PER_SEC;
  *_retval = PR_FALSE;

  while ((rv = enumerator->HasMoreElements (&has_more)) && has_more) {
    nsISupports *element;
    rv = enumerator->GetNext (&element);
    if (NS_FAILED (rv))
      break;

    nsICookie2 *cookie = static_cast<nsICookie2 *>(element);

    // There's no need to check the return values, we know our cookie
    // implementation always returns NS_OK (implementation above)
    PRInt64 expiry;
    cookie->GetExpiry (&expiry);
    if (expiry > now) {
      nsCAutoString chost, cname, cpath;

      cookie->GetHost (chost);
      cookie->GetName (cname);
      cookie->GetPath (cpath);

      if (chost.Equals (host) &&
          cname.Equals (name) &&
          cpath.Equals (path)) {
        *_retval = PR_TRUE;
        break;
      }
    }
  }
  NS_RELEASE (enumerator);

  if (NS_FAILED (rv))
    return rv;

  return rv;
}

NS_IMETHODIMP
HeadlessCookieService::CountCookiesFromHost (const nsACString &aHost,
                                             PRUint32         *_retval NS_OUTPARAM)
{
  guint count;
  gboolean result;
  GError *error = NULL;
  guint ns_result = NS_OK;

  result = mhs_cookies_count_cookies_from_host (mMhsCookies,
                                                nsCString (aHost).get (),
                                                &count,
                                                &error);

  if (!result) {
    ns_result = mhs_error_to_nsresult (error);
    g_warning ("Error counting cookies from host: %s", error->message);
    g_error_free (error);
  } else
    *_retval = count;

  return (nsresult)ns_result;
}

NS_IMETHODIMP
HeadlessCookieService::ImportCookies (nsIFile *aCookieFile)
{
  char *path;
  gboolean result;
  nsAutoString path16;
  GError *error = NULL;
  guint ns_result = NS_OK;

  nsresult rv = aCookieFile->GetPath (path16);
  if (NS_FAILED (rv))
      return rv;

  path = ToNewUTF8String (path16);
  result = mhs_cookies_import_cookies (mMhsCookies,
                                       path,
                                       &error);
  NS_Free (path);

  if (!result) {
    ns_result = mhs_error_to_nsresult (error);
    g_warning ("Error importing cookies: %s", error->message);
    g_error_free (error);
  }

  return (nsresult)ns_result;
}


NS_GENERIC_FACTORY_SINGLETON_CONSTRUCTOR(HeadlessCookieService, HeadlessCookieService::GetSingleton)

static const nsModuleComponentInfo cookieServiceComp = {
  "Headless Cookie Service",
  NS_COOKIESERVICE_CID,
  NS_COOKIESERVICE_CONTRACTID,
  HeadlessCookieServiceConstructor
};

static const nsModuleComponentInfo cookieManagerComp = {
  "Headless Cookie Manager",
  NS_COOKIEMANAGER_CID,
  NS_COOKIEMANAGER_CONTRACTID,
  HeadlessCookieServiceConstructor
};

void
clutter_mozheadless_cookies_init ()
{
  static gboolean comp_is_registered = FALSE;

  if (!comp_is_registered)
    {
      moz_headless_register_component ((gpointer)&cookieServiceComp);
      moz_headless_register_component ((gpointer)&cookieManagerComp);
      comp_is_registered = TRUE;
    }
}

void
clutter_mozheadless_cookies_deinit ()
{
  if (HeadlessCookieService::sHeadlessCookieService)
    delete HeadlessCookieService::sHeadlessCookieService;
}

