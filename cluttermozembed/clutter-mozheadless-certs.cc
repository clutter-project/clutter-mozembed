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
 */

#include <nsICertOverrideService.h>
#include <nsWeakReference.h>
#include <nsStringGlue.h>
#include <nsIGenericFactory.h>

#include "clutter-mozheadless.h"
#include "clutter-mozheadless-certs.h"

#include <stdlib.h>
#include <stdio.h>

class HeadlessCertOverrideService: public nsICertOverrideService,
                                   public nsSupportsWeakReference
{
 public:
  HeadlessCertOverrideService();
  virtual ~HeadlessCertOverrideService();

  static HeadlessCertOverrideService *GetSingleton();

  NS_DECL_ISUPPORTS
  NS_DECL_NSICERTOVERRIDESERVICE

  nsresult GetStatusForURL (const gchar *source_url, gboolean *bad_cert);

  static HeadlessCertOverrideService *sHeadlessCertOverrideService;

 private:
  GHashTable *mBadCerts;
};

HeadlessCertOverrideService *HeadlessCertOverrideService::sHeadlessCertOverrideService = nsnull;

HeadlessCertOverrideService::HeadlessCertOverrideService()
{
  mBadCerts = g_hash_table_new_full (g_str_hash,
                                     g_str_equal,
                                     g_free,
                                     NULL);
}

HeadlessCertOverrideService::~HeadlessCertOverrideService()
{
  if (mBadCerts) {
    g_hash_table_unref (mBadCerts);
    mBadCerts = nsnull;
  }

  if (sHeadlessCertOverrideService == this)
    sHeadlessCertOverrideService = nsnull;
}

HeadlessCertOverrideService *
HeadlessCertOverrideService::GetSingleton()
{
  if (!sHeadlessCertOverrideService) {
    sHeadlessCertOverrideService = new HeadlessCertOverrideService ();
  }

  return sHeadlessCertOverrideService;
}

nsresult
HeadlessCertOverrideService::GetStatusForURL (const gchar *source_url,
                                              gboolean    *invalid_cert)
{
  gchar *hostname, *ptr, *url;
  gint port = 0;
  gboolean badcert = FALSE;

  if (!source_url || !invalid_cert)
    return NS_ERROR_INVALID_ARG;

  url = g_strdup (source_url);

  /* Only check full URLs with a protocol or about: */
  ptr = strstr (url, "://");
  if (!ptr)
    {
      if (!strncmp (url, "about:", 6))
        goto cert_update;
      g_free (url);
      return NS_ERROR_INVALID_ARG;
    }

  ptr[0] = '\0';

  if (strcmp (url, "https"))
    goto cert_update;

  hostname = ptr + 3;

  ptr = strchr (hostname, '/');
  if (ptr)
    ptr[0] = '\0';

  ptr = strchr (hostname, ':');
  if (ptr)
    {
      ptr[0] = '\0';
      port = atoi (ptr + 1);
    }

  if (!port)
    port = 443;

  ptr = g_strdup_printf ("%s:%d", hostname, port);
  if (g_hash_table_lookup (mBadCerts, ptr))
    badcert = TRUE;
  g_free (ptr);

 cert_update:
  *invalid_cert = badcert;
  g_free (url);

  return NS_OK;
}

NS_IMETHODIMP_(nsrefcnt)
HeadlessCertOverrideService::AddRef ()
{
  return 1;
}

NS_IMETHODIMP_(nsrefcnt)
HeadlessCertOverrideService::Release ()
{
  return 1;
}

NS_INTERFACE_MAP_BEGIN(HeadlessCertOverrideService)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsICertOverrideService)
  NS_INTERFACE_MAP_ENTRY(nsICertOverrideService)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
NS_INTERFACE_MAP_END

NS_IMETHODIMP
HeadlessCertOverrideService::RememberValidityOverride(const nsACString & aHostName,
                                                      PRInt32 aPort,
                                                      nsIX509Cert *aCert,
                                                      PRUint32 aOverrideBits,
                                                      PRBool aTemporary)
{
  return NS_OK;
}

NS_IMETHODIMP
HeadlessCertOverrideService::HasMatchingOverride(const nsACString & aHostName,
                                                 PRInt32 aPort,
                                                 nsIX509Cert *aCert, 
                                                 PRUint32 *aOverrideBits,
                                                 PRBool *aIsTemporary,
                                                 PRBool *_retval)
{
  gchar *location;

  NS_ENSURE_ARG_POINTER(aOverrideBits);
  NS_ENSURE_ARG_POINTER(aIsTemporary);

  location = g_strdup_printf ("%s:%d", aHostName.BeginReading(), aPort);

  if (!g_hash_table_lookup (mBadCerts, location))
    g_hash_table_insert (mBadCerts, location, GINT_TO_POINTER (1));

  *aOverrideBits = ERROR_UNTRUSTED | ERROR_MISMATCH | ERROR_TIME;
  *aIsTemporary = PR_TRUE;
  *_retval = PR_TRUE;
  return NS_OK;
}

NS_IMETHODIMP
HeadlessCertOverrideService::GetValidityOverride(const nsACString & aHostName,
                                                 PRInt32 aPort,
                                                 nsACString & aHashAlg, 
                                                 nsACString & aFingerprint, 
                                                 PRUint32 *aOverrideBits,
                                                 PRBool *aIsTemporary,
                                                 PRBool *_found)
{
  NS_ENSURE_ARG_POINTER(aOverrideBits);
  NS_ENSURE_ARG_POINTER(aIsTemporary);

  aHashAlg = "";
  aFingerprint = "";
  *aOverrideBits = ERROR_UNTRUSTED; // | ERROR_MISMATCH | ERROR_TIME;
  *aIsTemporary = PR_TRUE;
  *_found = PR_TRUE;
  return NS_OK;
}

NS_IMETHODIMP
HeadlessCertOverrideService::ClearValidityOverride(const nsACString & aHostName,
                                                   PRInt32 aPort)
{
  return NS_OK;
}

NS_IMETHODIMP
HeadlessCertOverrideService::GetAllOverrideHostsWithPorts(PRUint32 *aCount, 
                                                          PRUnichar ***aHostsWithPortsArray)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HeadlessCertOverrideService::IsCertUsedForOverrides(nsIX509Cert *aCert, 
                                                    PRBool aCheckTemporaries,
                                                    PRBool aCheckPermanents,
                                                    PRUint32 *_retval)
{
  NS_ENSURE_ARG(_retval);

  *_retval = 1;
  return NS_OK;
}

/* generated locally because not present in nsICertOverrideService.idl */
#define NS_CERTOVERRIDE_CID                            \
  { /* {a3e30986-2d62-4a2a-8f9e-da0bbfe4fbe7} */       \
    0xa3e30986,                                        \
    0x2d62,                                            \
    0x4a2a,                                            \
    { 0x84, 0x9e, 0xda, 0x0b, 0xbf, 0xe4, 0xfb, 0xe7 } \
  }

NS_GENERIC_FACTORY_SINGLETON_CONSTRUCTOR(HeadlessCertOverrideService, HeadlessCertOverrideService::GetSingleton)

static const nsModuleComponentInfo certOverrideServiceComp = {
  "Certificate Override Service",
  NS_CERTOVERRIDE_CID,
  NS_CERTOVERRIDE_CONTRACTID,
  HeadlessCertOverrideServiceConstructor
};

void
clutter_mozheadless_certs_init ()
{
  static gboolean comp_is_registered = FALSE;

  if (!comp_is_registered)
    {
      moz_headless_register_component ((gpointer)&certOverrideServiceComp);
      comp_is_registered = TRUE;
    }
}

void
clutter_mozheadless_certs_deinit ()
{
  if (HeadlessCertOverrideService::sHeadlessCertOverrideService)
    delete HeadlessCertOverrideService::sHeadlessCertOverrideService;
}

void
clutter_mozheadless_update_cert_status (const gchar *url,
                                        gboolean    *invalid_cert)
{
  /* if we confirm good or bad certificate status, update invalid_cert;
     otherwise, leave it at current state */

  HeadlessCertOverrideService *service;
  service = HeadlessCertOverrideService::sHeadlessCertOverrideService;

  if (!service)
    return;

  service->GetStatusForURL(url, invalid_cert);
}
