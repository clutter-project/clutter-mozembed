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

#include <nsComponentManagerUtils.h>
#include <nsCOMPtr.h>
#include <nsIComponentManager.h>
#include <nsIDOMWindow.h>
#include <nsIHelperAppLauncherDialog.h>
#include <nsIExternalHelperAppService.h>
#include <nsIFilePicker.h>
#include <nsIGenericFactory.h>
#include <nsIInterfaceRequestorUtils.h>
#include <nsIIOService.h>
#include <nsIURI.h>
#include <nsIWebProgress.h>
#include <nsIWebProgressListener.h>
#include <nsIWebProgressListener2.h>
#include <nsStringGlue.h>

#include "clutter-mozheadless-downloads.h"
#include <moz-headless.h>
#include <mhs/mhs.h>

class HeadlessDownloads : public nsIHelperAppLauncherDialog,
                          public nsIWebProgressListener2 {
public:
  HeadlessDownloads();
  virtual ~HeadlessDownloads();

  NS_DECL_ISUPPORTS
  NS_DECL_NSIHELPERAPPLAUNCHERDIALOG
  NS_DECL_NSIWEBPROGRESSLISTENER
  NS_DECL_NSIWEBPROGRESSLISTENER2
};

HeadlessDownloads::HeadlessDownloads()
{
}

HeadlessDownloads::~HeadlessDownloads()
{
}

NS_IMPL_ISUPPORTS3(HeadlessDownloads,
                   nsIHelperAppLauncherDialog,
                   nsIWebProgressListener,
                   nsIWebProgressListener2)

// nsIHelperAppLauncherDialog

NS_IMETHODIMP
HeadlessDownloads::Show(nsIHelperAppLauncher *aLauncher,
                        nsISupports          *aContext,
                        PRUint32              aReason)
{
  return aLauncher->SaveToDisk(nsnull, PR_FALSE);
}

NS_IMETHODIMP
HeadlessDownloads::PromptForSaveToFile(nsIHelperAppLauncher  *aLauncher,
                                       nsISupports           *aWindowContext,
                                       const PRUnichar       *aDefaultFile,
                                       const PRUnichar       *aSuggestedFileExtension,
                                       PRBool                 aForcePrompt,
                                       nsILocalFile         **_retval)
{
  NS_ENSURE_ARG_POINTER(_retval);
  *_retval = nsnull;

  nsresult rv;

  /* Get window */
  nsCOMPtr<nsIDOMWindow> window(do_GetInterface(aWindowContext));
  NS_ENSURE_TRUE(window, NS_ERROR_FAILURE);

  /* Create a file dialog */
  nsCOMPtr<nsIFilePicker> filePicker(do_CreateInstance("@mozilla.org/filepicker;1", &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  /* Initialise file dialog */
  /* FIXME: i18n */
  rv = filePicker->Init(window, NS_LITERAL_STRING("Save As..."), nsIFilePicker::modeSave);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = filePicker->AppendFilters(nsIFilePicker::filterAll);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = filePicker->SetDefaultString(nsString (aDefaultFile));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = filePicker->SetDefaultExtension(nsString (aSuggestedFileExtension));
  NS_ENSURE_SUCCESS(rv, rv);

  /* Get the file */
  PRInt16 result;
  rv = filePicker->Show(&result);
  if (NS_FAILED (rv) || (result == nsIFilePicker::returnCancel)) {
    return NS_ERROR_FAILURE;
  } else {
    // Add ourselves as the listener for the download. Note that this will
    // add a reference, which will be released when the download is finished
    aLauncher->SetWebProgressListener (this);
  }

  return filePicker->GetFile(_retval);
}

// nsIWebProgressListener

NS_IMETHODIMP
HeadlessDownloads::OnStateChange(nsIWebProgress *aWebProgress,
                                 nsIRequest     *aRequest,
                                 PRUint32        aStateFlags,
                                 nsresult        aStatus)
{
  return NS_OK;
}

NS_IMETHODIMP
HeadlessDownloads::OnProgressChange(nsIWebProgress *aWebProgress,
                                    nsIRequest     *aRequest,
                                    PRInt32         aCurSelfProgress,
                                    PRInt32         aMaxSelfProgress,
                                    PRInt32         aCurTotalProgress,
                                    PRInt32         aMaxTotalProgress)
{
  return OnProgressChange64 (aWebProgress,
                             aRequest,
                             aCurSelfProgress,
                             aMaxSelfProgress,
                             aCurTotalProgress,
                             aMaxTotalProgress);
}

NS_IMETHODIMP
HeadlessDownloads::OnLocationChange(nsIWebProgress *aWebProgress,
                                    nsIRequest     *aRequest,
                                    nsIURI         *aLocation)
{
  return NS_OK;
}

NS_IMETHODIMP
HeadlessDownloads::OnStatusChange(nsIWebProgress  *aWebProgress,
                                  nsIRequest      *aRequest,
                                  nsresult         aStatus,
                                  const PRUnichar *aMessage)
{
  return NS_OK;
}

NS_IMETHODIMP
HeadlessDownloads::OnSecurityChange(nsIWebProgress *aWebProgress,
                                    nsIRequest     *aRequest,
                                    PRUint32        aState)
{
  return NS_OK;
}

// nsIWebProgressListener2

NS_IMETHODIMP
HeadlessDownloads::OnProgressChange64(nsIWebProgress *aWebProgress,
                                      nsIRequest     *aRequest,
                                      PRInt64         aCurSelfProgress,
                                      PRInt64         aMaxSelfProgress,
                                      PRInt64         aCurTotalProgress,
                                      PRInt64         aMaxTotalProgress)
{
  g_debug ("Progress: %Ld/%Ld", aCurTotalProgress, aMaxTotalProgress);
  return NS_OK;
}

NS_IMETHODIMP
HeadlessDownloads::OnRefreshAttempted(nsIWebProgress *aWebProgress,
                                      nsIURI         *aRefreshURI,
                                      PRInt32         aMillis,
                                      PRBool          aSameURI,
                                      PRBool         *_retval NS_OUTPARAM)
{
  return NS_OK;
}

#define HEADLESS_DOWNLOADS_CID \
  { 0xf578f2fb, 0xfe2b, 0x46a0, \
  { 0xb3, 0x90, 0x66, 0x39, 0x4a, 0xa6, 0x5d, 0xc8 } }
NS_GENERIC_FACTORY_CONSTRUCTOR(HeadlessDownloads)

static const nsModuleComponentInfo downloadsHelperComp = {
  "Headless download helper",
  HEADLESS_DOWNLOADS_CID,
  NS_IHELPERAPPLAUNCHERDLG_CONTRACTID,
  HeadlessDownloadsConstructor
};


void
clutter_mozheadless_downloads_init ()
{
  static gboolean comp_is_registered = FALSE;

  if (!comp_is_registered)
    {
      moz_headless_register_component ((gpointer)&downloadsHelperComp);
      comp_is_registered = TRUE;
    }
}

void
clutter_mozheadless_downloads_deinit ()
{
}

