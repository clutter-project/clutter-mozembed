/*
 * ClutterMozembed; a ClutterActor that embeds Mozilla
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

#ifndef _CLUTTER_MOZEMBED_PRIVATE
#define _CLUTTER_MOZEMBED_PRIVATE

#include "clutter-mozembed.h"
#include "clutter-mozembed-download.h"

ClutterMozEmbedDownload *clutter_mozembed_download_new (gint id,
                                                        const gchar *source,
                                                        const gchar *dest);
void clutter_mozembed_download_set_progress (ClutterMozEmbedDownload *download,
                                             gint64                   progress,
                                             gint64                   max_progress);
void clutter_mozembed_download_set_complete (ClutterMozEmbedDownload *download,
                                             gboolean                 complete);

#endif /* _CLUTTER_MOZEMBED_PRIVATE */

