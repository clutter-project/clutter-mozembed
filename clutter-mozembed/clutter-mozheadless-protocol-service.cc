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

#include <nsIExternalProtocolService.h>
#include <nsCExternalHandlerService.h>
#include <nsWeakReference.h>
#include <nsIGenericFactory.h>
#include <nsIIOService.h>
#include <nsIObserverService.h>
#include <nsIURI.h>
#include <nsNetUtil.h>
#include <nsStringGlue.h>

#include "clutter-mozheadless.h"
#include "clutter-mozheadless-protocol-service.h"
#include <moz-headless.h>
#include <mhs/mhs.h>

class HeadlessProtocolService : public nsIExternalProtocolService,
                                public nsSupportsWeakReference
{
 public:
  HeadlessProtocolService();
  virtual ~HeadlessProtocolService();

  static HeadlessProtocolService *GetSingleton(void);
  static HeadlessProtocolService *sHeadlessProtocolService;

  NS_DECL_ISUPPORTS
  NS_DECL_NSIEXTERNALPROTOCOLSERVICE
};

HeadlessProtocolService::HeadlessProtocolService(void)
{
}

HeadlessProtocolService::~HeadlessProtocolService()
{
  if (sHeadlessProtocolService == this)
    sHeadlessProtocolService = nsnull;
}

HeadlessProtocolService *HeadlessProtocolService::sHeadlessProtocolService = nsnull;

HeadlessProtocolService *
HeadlessProtocolService::GetSingleton(void)
{
  if (!sHeadlessProtocolService) {
    sHeadlessProtocolService = new HeadlessProtocolService ();
  }

  return sHeadlessProtocolService;
}

NS_IMETHODIMP_(nsrefcnt)
HeadlessProtocolService::AddRef ()
{
  return 1;
}

NS_IMETHODIMP_(nsrefcnt)
HeadlessProtocolService::Release ()
{
  return 1;
}

NS_IMETHODIMP
HeadlessProtocolService::ExternalProtocolHandlerExists (const char *aProtocolScheme,
                                                        PRBool     *_retval NS_OUTPARAM)
{
  GAppInfo *info = g_app_info_get_default_for_uri_scheme (aProtocolScheme);

  if (info) {
    g_object_unref (G_OBJECT (info));
    *_retval = PR_TRUE;
  } else {
    *_retval = PR_FALSE;
  }

  return NS_OK;
}

NS_IMETHODIMP
HeadlessProtocolService::IsExposedProtocol (const char *aProtocolScheme,
                                            PRBool     *_retval NS_OUTPARAM)
{
  ExternalProtocolHandlerExists (aProtocolScheme, _retval);

  *_retval = !(*_retval);
  if (!(*_retval)) {
    // See if mozilla can handle this protocol, we don't want to override
    // for things like http or ftp
    gchar *protocol_cid = g_strconcat ("@mozilla.org/network/protocol;1?name=",
                                       aProtocolScheme, NULL);
    nsCOMPtr<nsIProtocolHandler> handler = do_GetService (protocol_cid);
    if (handler)
      *_retval = PR_TRUE;
  }

  return NS_OK;
}

NS_IMETHODIMP
HeadlessProtocolService::GetProtocolHandlerInfo (const nsACString  &aProtocolScheme,
                                                 nsIHandlerInfo   **_retval NS_OUTPARAM)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HeadlessProtocolService::GetProtocolHandlerInfoFromOS (const nsACString  &aProtocolScheme,
                                                       PRBool            *aFound NS_OUTPARAM,
                                                       nsIHandlerInfo   **_retval NS_OUTPARAM)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HeadlessProtocolService::SetProtocolHandlerDefaults (nsIHandlerInfo *aHandlerInfo,
                                                     PRBool          aOSHandlerExists)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HeadlessProtocolService::LoadUrl (nsIURI *aURL)
{
  nsCAutoString url;
  nsresult rv = aURL->GetSpec(url);
  if (NS_FAILED(rv))
    return rv;

  GAppLaunchContext *context = g_app_launch_context_new ();
  gboolean result = g_app_info_launch_default_for_uri (url.get (), context, NULL);
  g_object_unref (G_OBJECT (context));

  return result ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
HeadlessProtocolService::LoadURI (nsIURI                *aURI,
                                  nsIInterfaceRequestor *aWindowContext)
{
  //nsCOMPtr<nsIDOMWindow> window(do_GetInterface (aWindowContext));
  return LoadUrl (aURI);
}

NS_IMETHODIMP
HeadlessProtocolService::GetApplicationDescription (const nsACString &aScheme,
                                                    nsAString        &_retval NS_OUTPARAM)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMPL_QUERY_INTERFACE2(HeadlessProtocolService,
                         nsIExternalProtocolService,
                         nsISupportsWeakReference)

#define HEADLESS_PROTOCOL_SERVICE_CID \
  {0xcf85b3a3, 0xb100, 0x43e3, {0xaf, 0xfb, 0x83, 0x33, 0x46, 0xe0, 0x87, 0x2b}}
NS_GENERIC_FACTORY_SINGLETON_CONSTRUCTOR(HeadlessProtocolService, HeadlessProtocolService::GetSingleton)

static const nsModuleComponentInfo protocolServiceComp = {
  "External protocol service",
  HEADLESS_PROTOCOL_SERVICE_CID,
  NS_EXTERNALPROTOCOLSERVICE_CONTRACTID,
  HeadlessProtocolServiceConstructor
};

void
clutter_mozheadless_protocol_service_init ()
{
  static gboolean comp_is_registered = FALSE;

  if (!comp_is_registered)
    {
      moz_headless_register_component ((gpointer)&protocolServiceComp);
      comp_is_registered = TRUE;
    }
}

void
clutter_mozheadless_protocol_service_deinit ()
{
  if (HeadlessProtocolService::sHeadlessProtocolService) {
    delete HeadlessProtocolService::sHeadlessProtocolService;
    HeadlessProtocolService::sHeadlessProtocolService = nsnull;
  }
}

