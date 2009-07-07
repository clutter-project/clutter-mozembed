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

#ifndef _CLUTTER_MOZHEADLESS_CERTS_H
#define _CLUTTER_MOZHEADLESS_CERTS_H

#include <glib.h>

G_BEGIN_DECLS

void clutter_mozheadless_certs_init ();
void clutter_mozheadless_certs_deinit ();

void clutter_mozheadless_update_cert_status (const gchar *url,
                                             gboolean *invalid_cert);

G_END_DECLS

#endif
