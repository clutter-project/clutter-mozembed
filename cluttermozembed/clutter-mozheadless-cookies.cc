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
#include <nsCOMPtr.h>
#include <nsMemory.h>
#include <nsServiceManagerUtils.h>
#include <nsWeakReference.h>
#include <nsStringGlue.h>
#include <nsCRTGlue.h>

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
      g_free (mEnd);
    }

 private:
  HeadlessCookie(gchar   *aName,
                 gchar   *aValue,
                 gchar   *aHost,
                 gchar   *aPath,
                 gchar   *aEnd,
                 PRInt64  aExpiry,
                 PRInt64  aLastAccessed,
                 PRInt64  aCreationID,
                 PRBool   aIsSession,
                 PRBool   aIsSecure,
                 PRBool   aIsHttpOnly)
    : mName (aName)
    , mValue (aValue)
    , mHost (aHost)
    , mPath (aPath)
    , mEnd (aEnd)
    , mExpiry (aExpiry)
    , mLastAccessed (aLastAccessed)
    , mCreationID (aCreationID)
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
  gchar        *mEnd;
  PRInt64       mExpiry;
  PRInt64       mLastAccessed;
  PRInt64       mCreationID;
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
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HeadlessCookie::GetValue (nsACString &aValue)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HeadlessCookie::GetIsDomain (PRBool *aIsDomain)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HeadlessCookie::GetHost (nsACString &aHost)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HeadlessCookie::GetPath (nsACString &aPath)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HeadlessCookie::GetIsSecure (PRBool *aIsSecure)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HeadlessCookie::GetExpires (PRUint64 *aExpires)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HeadlessCookie::GetStatus (nsCookieStatus *aStatus)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HeadlessCookie::GetPolicy (nsCookiePolicy *aPolicy)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

///////////////////////////////
// nsICookie2 implementation //
///////////////////////////////

NS_IMETHODIMP
HeadlessCookie::GetRawHost (nsACString &aRawHost)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HeadlessCookie::GetIsSession (PRBool *aIsSession)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HeadlessCookie::GetExpiry (PRInt64 *aExpiry)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HeadlessCookie::GetIsHttpOnly (PRBool *aIsHttpOnly)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HeadlessCookie::GetCreationTime (PRInt64 *aCreationTime)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

///////////////////////////
// HeadlessCookieService //
///////////////////////////

HeadlessCookieService::HeadlessCookieService(void)
{
}

HeadlessCookieService::~HeadlessCookieService()
{
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
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HeadlessCookieService::GetCookieStringFromHttp (nsIURI      *aURI,
                                                nsIURI      *aFirstURI,
                                                nsIChannel  *aChannel,
                                                char       **_retval NS_OUTPARAM)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HeadlessCookieService::SetCookieString (nsIURI     *aURI,
                                        nsIPrompt  *aPrompt,
                                        const char *aCookie,
                                        nsIChannel *aChannel)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HeadlessCookieService::SetCookieStringFromHttp (nsIURI     *aURI,
                                                nsIURI     *aFirstURI,
                                                nsIPrompt  *aPrompt,
                                                const char *aCookie,
                                                const char *aServerTime,
                                                nsIChannel *aChannel)
{
    return NS_ERROR_NOT_IMPLEMENTED;
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

