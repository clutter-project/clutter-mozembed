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
#include <nsCOMPtr.h>
#include <nsMemory.h>
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

 private:
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

 protected:
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
    sHeadlessCookieService = new HeadlessCookieService ();
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
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HeadlessCookieService::GetEnumerator (nsISimpleEnumerator **aEnumerator)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HeadlessCookieService::Remove (const nsACString &aDomain,
                               const nsACString &aName,
                               const nsACString &aPath,
                               PRBool            aBlocked)
{
    return NS_ERROR_NOT_IMPLEMENTED;
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
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HeadlessCookieService::CookieExists (nsICookie2 *aCookie,
                                     PRBool     *_retval NS_OUTPARAM)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HeadlessCookieService::CountCookiesFromHost (const nsACString &aHost,
                                             PRUint32         *_retval NS_OUTPARAM)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HeadlessCookieService::ImportCookies (nsIFile *aCookieFile)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}


void
clutter_mozheadless_cookies_init ()
{
}

void
clutter_mozheadless_cookies_deinit ()
{
}

