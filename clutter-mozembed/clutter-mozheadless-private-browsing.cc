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

#include <nsServiceManagerUtils.h>
#include <nsComponentManagerUtils.h>
#include <nsCOMPtr.h>
#include <nsIPrivateBrowsingService.h>
#include <nsIGenericFactory.h>
#include <nsIObserverService.h>
#include <nsISupportsPrimitives.h>
#include <nsNetCID.h>
#include <nsStringAPI.h>

#include "clutter-mozheadless-private-browsing.h"
#include "clutter-mozheadless.h"

class HeadlessPrivateBrowsing : public nsIPrivateBrowsingService
{
public:
  HeadlessPrivateBrowsing();
  virtual ~HeadlessPrivateBrowsing();

  static HeadlessPrivateBrowsing *GetSingleton(void);
  static HeadlessPrivateBrowsing *sHeadlessPrivateBrowsing;

  NS_DECL_ISUPPORTS
  NS_DECL_NSIPRIVATEBROWSINGSERVICE

private:
  PRBool enabled;

  friend void clutter_mozheadless_private_browsing_enable (void);
};

HeadlessPrivateBrowsing::HeadlessPrivateBrowsing (void)
{
  enabled = PR_FALSE;
}

HeadlessPrivateBrowsing::~HeadlessPrivateBrowsing()
{
}

HeadlessPrivateBrowsing *HeadlessPrivateBrowsing::sHeadlessPrivateBrowsing =
  nsnull;

HeadlessPrivateBrowsing *
HeadlessPrivateBrowsing::GetSingleton(void)
{
  if (!sHeadlessPrivateBrowsing)
    sHeadlessPrivateBrowsing = new HeadlessPrivateBrowsing ();

  return sHeadlessPrivateBrowsing;
}

NS_IMETHODIMP_(nsrefcnt)
HeadlessPrivateBrowsing::AddRef ()
{
  return 1;
}

NS_IMETHODIMP_(nsrefcnt)
HeadlessPrivateBrowsing::Release ()
{
  return 1;
}

/* attribute boolean privateBrowsingEnabled; */
NS_IMETHODIMP
HeadlessPrivateBrowsing::GetPrivateBrowsingEnabled (PRBool *enabled)
{
  *enabled = this->enabled;
  return NS_OK;
}

NS_IMETHODIMP
HeadlessPrivateBrowsing::SetPrivateBrowsingEnabled (PRBool enabled)
{
  /* Private browsing can only be enabled directly by calling
     clutter_mozheadless_private_browsing_enable */
  return NS_ERROR_NOT_IMPLEMENTED;
}

/* readonly attribute boolean autoStarted; */
NS_IMETHODIMP
HeadlessPrivateBrowsing::GetAutoStarted (PRBool *auto_started)
{
  /* We don't support auto-starting */
  *auto_started = PR_FALSE;

  return NS_OK;
}

/* void removeDataFromDomain (in AUTF8String aDomain); */
NS_IMETHODIMP
HeadlessPrivateBrowsing::RemoveDataFromDomain (const nsACString &domain)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMPL_QUERY_INTERFACE1 (HeadlessPrivateBrowsing,
                          nsIPrivateBrowsingService)

// {44b38a00-f844-4313-84fb-a1b4cec5bbc2}
#define HEADLESS_PRIVATEBROWSING_CID \
  { 0x44b38a00, 0xf844, 0x4313, \
      { 0x84, 0xfb, 0xa1, 0xb4, 0xce, 0xc5, 0xbb, 0xc2 } }

NS_GENERIC_FACTORY_SINGLETON_CONSTRUCTOR
 (HeadlessPrivateBrowsing, HeadlessPrivateBrowsing::GetSingleton)

static const nsModuleComponentInfo privateBrowsingComps[] =
{
  {
    "Headless private browsing service",
    HEADLESS_PRIVATEBROWSING_CID,
    "@mozilla.org/privatebrowsing;1",
    HeadlessPrivateBrowsingConstructor
  },

  {
    "Headless private browsing service wrapper",
    HEADLESS_PRIVATEBROWSING_CID,
    NS_PRIVATE_BROWSING_SERVICE_CONTRACTID,
    HeadlessPrivateBrowsingConstructor
  }
};

void
clutter_mozheadless_private_browsing_init ()
{
  static gboolean comp_is_registered = FALSE;

  if (!comp_is_registered)
    {
      guint i;

      for (i = 0; i < G_N_ELEMENTS (privateBrowsingComps); i++)
        moz_headless_register_component ((gpointer) (privateBrowsingComps + i));
      comp_is_registered = TRUE;
    }
}

void
clutter_mozheadless_private_browsing_deinit ()
{
}

void
clutter_mozheadless_private_browsing_enable (void)
{
  HeadlessPrivateBrowsing *pbService = HeadlessPrivateBrowsing::GetSingleton ();

  if (!pbService->enabled)
    {
      nsresult rv;

      pbService->enabled = PR_TRUE;

      /* Notify observers */
      nsCOMPtr<nsIObserverService> obs =
        do_GetService (NS_OBSERVERSERVICE_CONTRACTID, &rv);
      if (NS_FAILED (rv))
        return;

      nsCOMPtr<nsISupportsPRBool> quitting =
        do_CreateInstance (NS_SUPPORTS_PRBOOL_CONTRACTID, &rv);
      if (NS_FAILED (rv))
        return;

      rv = quitting->SetData (PR_FALSE);
      if (NS_FAILED (rv))
        return;

      if (NS_SUCCEEDED (rv))
        {
          obs->NotifyObservers (quitting, "private-browsing-change-granted",
                                NS_LITERAL_STRING ("enter").get ());
          obs->NotifyObservers (quitting, "private-browsing",
                                NS_LITERAL_STRING ("enter").get ());
        }
    }
}
