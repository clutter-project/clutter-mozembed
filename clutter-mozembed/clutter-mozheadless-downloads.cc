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
#include <nsIPrefService.h>
#include <nsITimer.h>
#include <nsStringGlue.h>
#include <nsNetUtil.h>
#include <nsDirectoryServiceDefs.h>
#include <nsDirectoryServiceUtils.h>
#include <nsIWebBrowser.h>
#include <nsIWebBrowserPersist.h>

#include "clutter-mozheadless-downloads.h"
#include <moz-headless.h>

static void
_create_download_cb (ClutterMozHeadless *moz_headless,
                     const gchar        *uri,
                     const gchar        *target);

class HeadlessAppLauncherDialog : public nsIHelperAppLauncherDialog {
public:
  HeadlessAppLauncherDialog() {mMozHeadless = nsnull;}
  virtual ~HeadlessAppLauncherDialog() {}

  NS_DECL_ISUPPORTS
  NS_DECL_NSIHELPERAPPLAUNCHERDIALOG

private:
  MozHeadless                   *mMozHeadless;

  nsresult PromptWithFilePicker(nsIDOMWindow      *window,
                                const PRUnichar   *aDefaultFile,
                                const PRUnichar   *aSuggestedFileExtension,
                                nsILocalFile     **aFile);

  nsresult CalculateFilename(nsIFile           *download_dir,
                             const PRUnichar   *aDefaultFile,
                             nsILocalFile     **aFile);

  nsresult GetDownloadDir(nsIPrefBranch  *root_branch,
                          nsIFile       **download_dir);

  nsresult CheckFileOrPartExists(nsILocalFile *file,
                                 PRBool       *exists);
};

class HeadlessDownloads : public nsIWebProgressListener2 {
public:
  HeadlessDownloads(MozHeadless *mozheadless);
  virtual ~HeadlessDownloads();

  NS_DECL_ISUPPORTS
  NS_DECL_NSIWEBPROGRESSLISTENER
  NS_DECL_NSIWEBPROGRESSLISTENER2

  void     CancelDownload();

  gint                           mDownloadId;

  nsIHelperAppLauncher          *mLauncher;
  nsIWebBrowserPersist          *mPersist;

private:
  MozHeadless                   *mMozHeadless;
  gboolean                       mCancelled;
  nsCOMPtr<nsITimer>             mThrottleTimer;
  gint64                         mCurProgress;
  gint64                         mMaxProgress;

  static gint sDownloadId;

  static void TimeoutHandler (nsITimer *timer, void *data);
  void SendProgress ();
};

gint HeadlessDownloads::sDownloadId = 0;

HeadlessDownloads::HeadlessDownloads(MozHeadless *mozheadless)
{
  mMozHeadless = mozheadless;
  mDownloadId = sDownloadId ++;
  mLauncher = nsnull;
  mPersist = nsnull;
  mCancelled = FALSE;
}

static void
_cancel_download_cb (ClutterMozHeadless *moz_headless,
                     gint                id,
                     HeadlessDownloads  *download)
{
  if (download->mDownloadId == id)
    download->CancelDownload ();
}

HeadlessDownloads::~HeadlessDownloads()
{
  if (mMozHeadless)
    {
      g_signal_handlers_disconnect_by_func (mMozHeadless,
                                            (void *)_cancel_download_cb,
                                            (void *)this);

      if (!mCancelled)
        send_feedback_all (CLUTTER_MOZHEADLESS (mMozHeadless),
                           CME_FEEDBACK_DL_COMPLETE,
                           G_TYPE_INT, mDownloadId,
                           G_TYPE_INVALID);

      g_object_unref (G_OBJECT (mMozHeadless));
      mMozHeadless = NULL;
    }

  if (mThrottleTimer)
    mThrottleTimer->Cancel ();
}

NS_IMPL_ISUPPORTS2(HeadlessDownloads,
                   nsIWebProgressListener,
                   nsIWebProgressListener2)

void
HeadlessDownloads::CancelDownload()
{
  // Cancelling will release the reference on us and we'll dispose,
  // so send the cancelled message first.
  send_feedback_all (CLUTTER_MOZHEADLESS (mMozHeadless),
                     CME_FEEDBACK_DL_CANCELLED,
                     G_TYPE_INT, mDownloadId,
                     G_TYPE_INVALID);

  mCancelled = TRUE;
  if (mLauncher)
    mLauncher->Cancel(NS_ERROR_ABORT);
  else if (mPersist)
    mPersist->Cancel (NS_ERROR_ABORT);
  else
    g_warning ("Failed to cancel download due to NULL launcher");
}

// nsIWebProgressListener

NS_IMETHODIMP
HeadlessDownloads::OnStateChange(nsIWebProgress *aWebProgress,
                                 nsIRequest     *aRequest,
                                 PRUint32        aStateFlags,
                                 nsresult        aStatus)
{
  // WebBrowserPersist doesn't release the reference to the listener
  if (aStateFlags & STATE_STOP) {
    if (mPersist && !mLauncher)
      delete this;
  }

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

void
HeadlessDownloads::SendProgress ()
{
  if (mMozHeadless)
    send_feedback_all (CLUTTER_MOZHEADLESS (mMozHeadless),
                       CME_FEEDBACK_DL_PROGRESS,
                       G_TYPE_INT, mDownloadId,
                       G_TYPE_INT64, (gint64)mCurProgress,
                       G_TYPE_INT64, (gint64)mMaxProgress,
                       G_TYPE_INVALID);
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
  this->mCurProgress = aCurTotalProgress;
  this->mMaxProgress = aMaxTotalProgress;

  /* If there is already a timer started to throttle the progress
     reports then we don't need to do anything */
  if (!mThrottleTimer)
    {
      nsresult rv;

      SendProgress ();

      mThrottleTimer = do_CreateInstance ("@mozilla.org/timer;1", &rv);
      NS_ENSURE_SUCCESS (rv, rv);

      /* Start a timer so that we don't report the progress again for
         another 160ms */
      mThrottleTimer->InitWithFuncCallback (TimeoutHandler, this, 160,
                                            nsITimer::TYPE_ONE_SHOT);
      NS_ENSURE_SUCCESS (rv, rv);
    }

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


void
HeadlessDownloads::TimeoutHandler (nsITimer *timer, void *data)
{
  HeadlessDownloads *self = reinterpret_cast<HeadlessDownloads *> (data);

  self->mThrottleTimer = nsnull;

  self->SendProgress ();
}

NS_IMPL_ISUPPORTS1(HeadlessAppLauncherDialog,
                  nsIHelperAppLauncherDialog)

// nsIHelperAppLauncherDialog

NS_IMETHODIMP
HeadlessAppLauncherDialog::Show(nsIHelperAppLauncher *aLauncher,
                                nsISupports          *aContext,
                                PRUint32              aReason)
{
  return aLauncher->SaveToDisk(nsnull, PR_FALSE);
}

nsresult
HeadlessAppLauncherDialog::PromptWithFilePicker(nsIDOMWindow      *window,
                                                const PRUnichar   *aDefaultFile,
                                                const PRUnichar   *aSuggestedFileExtension,
                                                nsILocalFile     **aFile)
{
  nsresult rv;

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
  if (NS_FAILED (rv))
    return rv;
  if (result == nsIFilePicker::returnCancel)
    return NS_ERROR_FAILURE;
  return filePicker->GetFile(aFile);
}

nsresult
HeadlessAppLauncherDialog::CheckFileOrPartExists(nsILocalFile *file,
                                                 PRBool       *exists)
{
  nsresult rv;
  nsAutoString leaf_name;

  // Check whether the original file exists */
  rv = file->Exists (exists);
  if (NS_FAILED (rv) || *exists)
    return rv;

  // If that doesn't exist, check whether the file with ".part"
  // appended exists
  rv = file->GetLeafName (leaf_name);
  NS_ENSURE_SUCCESS (rv, rv);

  leaf_name.Append (NS_LITERAL_STRING (".part"));

  nsCOMPtr<nsIFile> fileCopy;
  nsCOMPtr<nsILocalFile> localFileCopy;

  rv = file->Clone (getter_AddRefs (fileCopy));
  NS_ENSURE_SUCCESS (rv, rv);
  localFileCopy = do_QueryInterface (fileCopy, &rv);
  NS_ENSURE_SUCCESS (rv, rv);
  rv = localFileCopy->SetLeafName (leaf_name);
  NS_ENSURE_SUCCESS (rv, rv);

  return localFileCopy->Exists (exists);
}

nsresult
HeadlessAppLauncherDialog::CalculateFilename(nsIFile           *download_dir,
                                             const PRUnichar   *aDefaultFile,
                                             nsILocalFile     **aFile)
{
  nsresult rv;
  nsCOMPtr<nsIFile> fileCopy;
  nsCOMPtr<nsILocalFile> localFileCopy;
  PRBool file_exists;

  rv = download_dir->Clone (getter_AddRefs (fileCopy));
  NS_ENSURE_SUCCESS (rv, rv);
  localFileCopy = do_QueryInterface (fileCopy, &rv);
  NS_ENSURE_SUCCESS (rv, rv);
  rv = localFileCopy->AppendRelativePath (nsDependentString (aDefaultFile));
  NS_ENSURE_SUCCESS (rv, rv);

  rv = CheckFileOrPartExists (localFileCopy, &file_exists);
  NS_ENSURE_SUCCESS (rv, rv);
  if (!file_exists)
    localFileCopy.forget (aFile);
  else
    {
      nsDependentString full_filename (aDefaultFile);
      PRInt32 dot_pos;
      int i = 2;
      nsAutoString filename;

      dot_pos = full_filename.RFindChar ('.');

      while (true)
        {
          filename.Truncate ();

          if (dot_pos == -1)
            filename.Append (full_filename);
          else
            filename.Append (Substring (full_filename, 0, dot_pos));

          filename.Append ('-');
          filename.AppendInt (i);

          if (dot_pos != -1)
            filename.Append (Substring (full_filename, dot_pos));

          rv = download_dir->Clone (getter_AddRefs (fileCopy));
          NS_ENSURE_SUCCESS (rv, rv);
          localFileCopy = do_QueryInterface (fileCopy, &rv);
          NS_ENSURE_SUCCESS (rv, rv);
          rv = localFileCopy->AppendRelativePath (filename);
          NS_ENSURE_SUCCESS (rv, rv);

          rv = CheckFileOrPartExists (localFileCopy, &file_exists);
          NS_ENSURE_SUCCESS (rv, rv);
          if (!file_exists)
            {
              localFileCopy.forget (aFile);
              break;
            }

          i++;
        }
    }

  return NS_OK;
}

nsresult
HeadlessAppLauncherDialog::GetDownloadDir(nsIPrefBranch  *root_branch,
                                          nsIFile       **download_dir)
{
  nsresult rv;
  nsCAutoString download_dir_str;

  rv = root_branch->GetCharPref ("clutter_mozembed."
                                 "download_directory",
                                 getter_Copies (download_dir_str));
  if (NS_SUCCEEDED (rv) && !download_dir_str.IsEmpty ())
    {
      nsCOMPtr<nsILocalFile> local_file;
      rv = NS_NewNativeLocalFile (download_dir_str, PR_FALSE,
                                  getter_AddRefs (local_file));
      NS_ENSURE_SUCCESS (rv, rv);
      return local_file->QueryInterface (NS_GET_IID (nsILocalFile),
                                         (void **) download_dir);
    }

  /* Try to get the XDG download dir */
  return NS_GetSpecialDirectory (NS_UNIX_XDG_DOWNLOAD_DIR, download_dir);
}

NS_IMETHODIMP
HeadlessAppLauncherDialog::PromptForSaveToFile(nsIHelperAppLauncher  *aLauncher,
                                               nsISupports           *aWindowContext,
                                               const PRUnichar       *aDefaultFile,
                                               const PRUnichar       *aSuggestedFileExtension,
                                               PRBool                 aForcePrompt,
                                               nsILocalFile         **_retval)
{
  nsresult rv;

  NS_ENSURE_ARG_POINTER(_retval);
  *_retval = nsnull;
  nsCOMPtr<nsILocalFile> file;
  nsCOMPtr<nsIURI> uri, file_uri;

  /* Get window */
  nsCOMPtr<nsIDOMWindow> window(do_GetInterface(aWindowContext));
  NS_ENSURE_TRUE(window, NS_ERROR_FAILURE);

  /* Check if there is a preference to disable the file picker */
  nsCOMPtr<nsIPrefService> pref_service =
    do_GetService (NS_PREFSERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS (rv, rv);

  PRBool disable_file_picker = FALSE;

  nsCOMPtr<nsIPrefBranch> root_branch;
  nsCOMPtr<nsIFile> download_dir;
  rv = pref_service->GetBranch("", getter_AddRefs (root_branch));
  if (NS_SUCCEEDED (rv))
    {
      rv = root_branch->GetBoolPref ("clutter_mozembed."
                                     "disable_download_file_picker",
                                     &disable_file_picker);
      if (NS_FAILED (rv) ||
          (disable_file_picker &&
           NS_FAILED (GetDownloadDir (root_branch,
                                      getter_AddRefs (download_dir)))))
        disable_file_picker = FALSE;
    }

  if (disable_file_picker)
    rv = CalculateFilename (download_dir,
                            aDefaultFile,
                            getter_AddRefs (file));
  else
    rv = PromptWithFilePicker (window,
                               aDefaultFile,
                               aSuggestedFileExtension,
                               getter_AddRefs (file));
  NS_ENSURE_SUCCESS (rv, rv);

  // Add a reference on the window that started this download, so it doesn't
  // get destroyed until the download finishes
  mMozHeadless = moz_headless_get_from_dom_window ((gpointer)window);

  if (mMozHeadless)
    {
      // Create and add the listener for the download. Note that this will
      // add a reference, which will be released when the download is finished
      HeadlessDownloads *download = new HeadlessDownloads(mMozHeadless);

      aLauncher->SetWebProgressListener (download);

      // Store a reference to the launcher so we can cancel downloads
      download->mLauncher = aLauncher;

      g_object_ref (mMozHeadless);

      g_signal_connect (mMozHeadless, "cancel-download",
                        G_CALLBACK (_cancel_download_cb), download);

      // Inform ClutterMozEmbed of this new download
      if (NS_SUCCEEDED (aLauncher->GetSource(getter_AddRefs (uri))) &&
          NS_SUCCEEDED (NS_NewFileURI (getter_AddRefs (file_uri), file)))
        {
          nsCAutoString ns_uri_string, ns_file_uri_string;
          if (NS_SUCCEEDED (uri->GetSpec(ns_uri_string)) &&
              NS_SUCCEEDED (file_uri->GetSpec(ns_file_uri_string)))
            {
              const char *uri_string = ns_uri_string.get ();
              const char *file_uri_string = ns_file_uri_string.get ();

              send_feedback_all (CLUTTER_MOZHEADLESS (mMozHeadless),
                                 CME_FEEDBACK_DL_START,
                                 G_TYPE_INT, download->mDownloadId,
                                 G_TYPE_STRING, uri_string,
                                 G_TYPE_STRING, file_uri_string,
                                 G_TYPE_INVALID);
            }
        }
    }
  else
    g_warning ("Couldn't find window for download");

  file.forget(_retval);

  return NS_OK;
}

#define HEADLESS_DOWNLOADS_CID \
  { 0xf578f2fb, 0xfe2b, 0x46a0, \
  { 0xb3, 0x90, 0x66, 0x39, 0x4a, 0xa6, 0x5d, 0xc8 } }
NS_GENERIC_FACTORY_CONSTRUCTOR(HeadlessAppLauncherDialog)

static const nsModuleComponentInfo downloadsHelperComp = {
  "Headless download helper",
  HEADLESS_DOWNLOADS_CID,
  NS_IHELPERAPPLAUNCHERDLG_CONTRACTID,
  HeadlessAppLauncherDialogConstructor
};

static void
_create_download_cb (ClutterMozHeadless *moz_headless,
                     const gchar        *uri,
                     const gchar        *target)
{
  const char *uri_string;
  const char *file_uri_string;
  HeadlessDownloads *download;
  nsCOMPtr<nsILocalFile> aFile;
  nsCOMPtr<nsIURI> aUri, aFileUri;
  nsCAutoString ns_uri_string, ns_file_uri_string;

  nsIWebBrowser *browser = (nsIWebBrowser*)
    moz_headless_get_web_browser (MOZ_HEADLESS (moz_headless));

  nsCOMPtr<nsIWebBrowserPersist> persist (do_GetInterface (browser));

  if (!persist)
    goto cmh_dl_error;

  if (!NS_SUCCEEDED (NS_NewURI (getter_AddRefs (aUri), uri)))
    goto cmh_dl_error;

  if (!NS_SUCCEEDED (NS_NewNativeLocalFile (nsDependentCString (target),
                                            PR_FALSE, getter_AddRefs (aFile))))
    goto cmh_dl_error;

  if (!NS_SUCCEEDED (NS_NewFileURI (getter_AddRefs (aFileUri), aFile)))
    goto cmh_dl_error;

  download = new HeadlessDownloads (MOZ_HEADLESS (moz_headless));
  if(!download)
    goto cmh_dl_error;

  persist->SetProgressListener (download);

  // Store a reference to the persisit so we can cancel downloads
  download->mPersist = persist;

  g_object_ref (moz_headless);

  g_signal_connect (moz_headless, "cancel-download",
                    G_CALLBACK (_cancel_download_cb), download);

  persist->SaveURI (aUri,
                    nsnull,
                    nsnull,
                    nsnull,
                    nsnull,
                    aFile);

  if (!NS_SUCCEEDED (aUri->GetSpec(ns_uri_string)) ||
      !NS_SUCCEEDED (aFileUri->GetSpec(ns_file_uri_string)))
    goto cmh_dl_error;

  uri_string = ns_uri_string.get();
  file_uri_string = ns_file_uri_string.get();

  send_feedback_all (CLUTTER_MOZHEADLESS (moz_headless),
                     CME_FEEDBACK_DL_START,
                     G_TYPE_INT, download->mDownloadId,
                     G_TYPE_STRING, uri_string,
                     G_TYPE_STRING, file_uri_string,
                     G_TYPE_INVALID);

  return;

cmh_dl_error:
  g_warning("cannot create download\n");
}

void
clutter_mozheadless_downloads_init (ClutterMozHeadless *moz_headless)
{
  static gboolean comp_is_registered = FALSE;

  if (!comp_is_registered)
    {
      moz_headless_register_component ((gpointer)&downloadsHelperComp);
      comp_is_registered = TRUE;
    }
  g_signal_connect (moz_headless, "create-download",
                      G_CALLBACK (_create_download_cb), NULL);
}

void
clutter_mozheadless_downloads_deinit ()
{
}

