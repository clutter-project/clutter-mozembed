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

#include <nsIGlobalHistory2.h>
#include <nsWeakReference.h>
#include <nsIGenericFactory.h>
#include <nsIIOService.h>
#include <nsIObserverService.h>
#include <nsIURI.h>
#include <nsNetUtil.h>
#include <nsStringGlue.h>

#include "clutter-mozheadless.h"
#include "clutter-mozheadless-history.h"
#include <moz-headless.h>
#include <mhs/mhs.h>

class HeadlessGlobalHistory : public nsIGlobalHistory2,
                              public nsSupportsWeakReference
{
 public:
  HeadlessGlobalHistory();
  virtual ~HeadlessGlobalHistory();

  static HeadlessGlobalHistory *GetSingleton(void);
  static HeadlessGlobalHistory *sHeadlessGlobalHistory;

  NS_DECL_ISUPPORTS
  NS_DECL_NSIGLOBALHISTORY2

  void SendLinkVisitedEvent (nsIURI *aURI);

 private:
  MhsHistory                   *mMhsHistory;
};

static void
_link_visited_cb (MhsHistory *history,
                  const gchar *uri,
                  HeadlessGlobalHistory *global_history)
{
  nsresult rv;
  nsCOMPtr<nsIIOService> io_service =
    do_GetService (NS_IOSERVICE_CONTRACTID, &rv);

  if (!NS_FAILED (rv))
    {
      nsDependentCString uri_cstring (uri);

      nsCOMPtr<nsIURI> ns_uri;
      rv = io_service->NewURI (uri_cstring, nsnull, nsnull, getter_AddRefs (ns_uri));

      if (!NS_FAILED (rv))
        global_history->SendLinkVisitedEvent (ns_uri);
    }
}

HeadlessGlobalHistory::HeadlessGlobalHistory(void)
{
  mMhsHistory = mhs_history_new ();

  g_signal_connect (mMhsHistory, "link-visited",
                    G_CALLBACK (_link_visited_cb), this);
}

HeadlessGlobalHistory::~HeadlessGlobalHistory()
{
  if (mMhsHistory) {
    g_signal_handlers_disconnect_by_func (mMhsHistory,
                                          (gpointer)_link_visited_cb,
                                          this);
    g_object_unref (mMhsHistory);
    mMhsHistory = NULL;
  }

  if (sHeadlessGlobalHistory == this)
    sHeadlessGlobalHistory = nsnull;
}

HeadlessGlobalHistory *HeadlessGlobalHistory::sHeadlessGlobalHistory = nsnull;

HeadlessGlobalHistory *
HeadlessGlobalHistory::GetSingleton(void)
{
  if (!sHeadlessGlobalHistory) {
    sHeadlessGlobalHistory = new HeadlessGlobalHistory ();
  }

  return sHeadlessGlobalHistory;
}

NS_IMETHODIMP_(nsrefcnt)
HeadlessGlobalHistory::AddRef ()
{
  return 1;
}

NS_IMETHODIMP_(nsrefcnt)
HeadlessGlobalHistory::Release ()
{
  return 1;
}

NS_IMPL_QUERY_INTERFACE2(HeadlessGlobalHistory,
                         nsIGlobalHistory2,
                         nsISupportsWeakReference)

NS_IMETHODIMP
HeadlessGlobalHistory::AddURI(nsIURI *aURI,
                              PRBool  aRedirect,
                              PRBool  aToplevel,
                              nsIURI *aReferrer)
{
  nsCAutoString uriString, refString;
  nsresult rv = aURI->GetSpec(uriString);
  if (NS_FAILED(rv))
    return rv;

  const char *referrer = NULL;
  if (aReferrer) {
    rv = aReferrer->GetSpec(refString);
    if (NS_FAILED(rv))
      return rv;
    referrer = refString.get();
  }

  GError *error = NULL;
  gboolean result =
    mhs_history_add_uri (mMhsHistory,
                         (const gchar *)uriString.get(),
                         (gboolean)aRedirect,
                         (gboolean)aToplevel,
                         (const gchar *)referrer,
                         &error);
  if (!result)
    {
      nsresult rv = mhs_error_to_nsresult (error);
      g_warning ("Error adding URI: %s", error->message);
      g_error_free (error);
      return rv;
    }

  return NS_OK;
}

NS_IMETHODIMP
HeadlessGlobalHistory::IsVisited(nsIURI *aURI, PRBool *_retval)
{
  nsCAutoString uriString;
  nsresult rv = aURI->GetSpec(uriString);
  if (NS_FAILED(rv))
    return rv;

  GError *error = NULL;
  gboolean is_visited = FALSE;
  gboolean result =
    mhs_history_is_visited (mMhsHistory,
                            (const gchar *)uriString.get(),
                            &is_visited,
                            &error);
  if (!result)
    {
      nsresult rv = mhs_error_to_nsresult (error);
      g_warning ("Error checking is-visited: %s", error->message);
      g_error_free (error);
      return rv;
    }
  else
    *_retval = is_visited;

  return NS_OK;
}

NS_IMETHODIMP
HeadlessGlobalHistory::SetPageTitle(nsIURI *aURI, const nsAString &aTitle)
{
  nsCAutoString uriString;
  nsresult rv = aURI->GetSpec(uriString);
  if (NS_FAILED(rv))
    return rv;

  char *title_utf8 = ToNewUTF8String(aTitle);

  GError *error = NULL;
  gboolean result =
    mhs_history_set_page_title (mMhsHistory,
                                (const gchar *)uriString.get(),
                                (const gchar *)title_utf8,
                                &error);
  if (!result)
    {
      rv = mhs_error_to_nsresult (error);
      g_warning ("Error setting page title: %s", error->message);
      g_error_free (error);
    }
  else
    rv = NS_OK;

  NS_Free (title_utf8);

  return rv;
}

void
HeadlessGlobalHistory::SendLinkVisitedEvent (nsIURI *aURI)
{
  nsCOMPtr<nsIObserverService> obsService =
    do_GetService("@mozilla.org/observer-service;1");
  if (obsService)
    obsService->NotifyObservers(aURI, NS_LINK_VISITED_EVENT_TOPIC, nsnull);
}

#define HEADLESS_GLOBALHISTORY_CID \
  {0x45f4a193, 0xe8ec, 0x4687, {0xa9, 0x29, 0xf8, 0x5c, 0x92, 0x49, 0x40, 0x31}}
NS_GENERIC_FACTORY_SINGLETON_CONSTRUCTOR(HeadlessGlobalHistory, HeadlessGlobalHistory::GetSingleton)

static const nsModuleComponentInfo historyServiceComp = {
  "Global history",
  HEADLESS_GLOBALHISTORY_CID,
  "@mozilla.org/browser/global-history;2",
  HeadlessGlobalHistoryConstructor
};

void
clutter_mozheadless_history_init ()
{
  static gboolean comp_is_registered = FALSE;

  if (!comp_is_registered)
    {
      moz_headless_register_component ((gpointer)&historyServiceComp);
      comp_is_registered = TRUE;
    }
}

void
clutter_mozheadless_history_deinit ()
{
  if (HeadlessGlobalHistory::sHeadlessGlobalHistory)
    delete HeadlessGlobalHistory::sHeadlessGlobalHistory;
}

