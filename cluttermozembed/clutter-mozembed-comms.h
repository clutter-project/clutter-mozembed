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

#ifndef _CLUTTER_MOZEMBED_COMMS
#define _CLUTTER_MOZEMBED_COMMS

#include <glib.h>

typedef enum
{
  CME_FEEDBACK_UPDATE = 1,
  CME_FEEDBACK_MOTION_ACK,
  CME_FEEDBACK_PROGRESS,
  CME_FEEDBACK_NET_START,
  CME_FEEDBACK_NET_STOP,
  CME_FEEDBACK_LOCATION,
  CME_FEEDBACK_TITLE,
  CME_FEEDBACK_ICON,
  CME_FEEDBACK_CAN_GO_BACK,
  CME_FEEDBACK_CAN_GO_FORWARD,
  CME_FEEDBACK_NEW_WINDOW,
  CME_FEEDBACK_CLOSED,
  CME_FEEDBACK_LINK_MESSAGE,
  CME_FEEDBACK_SIZE_REQUEST,
  CME_FEEDBACK_SHM_NAME,
  CME_FEEDBACK_CURSOR,
  CME_FEEDBACK_SECURITY,
  CME_FEEDBACK_DL_START,
  CME_FEEDBACK_DL_PROGRESS,
  CME_FEEDBACK_DL_COMPLETE,
  CME_FEEDBACK_SHOW_TOOLTIP,
  CME_FEEDBACK_HIDE_TOOLTIP,
  CME_FEEDBACK_PRIVATE
} ClutterMozEmbedFeedback;

typedef enum
{
  CME_COMMAND_UPDATE_ACK = 1,
  CME_COMMAND_OPEN_URL,
  CME_COMMAND_RESIZE,
  CME_COMMAND_MOTION,
  CME_COMMAND_BUTTON_PRESS,
  CME_COMMAND_BUTTON_RELEASE,
  CME_COMMAND_KEY_PRESS,
  CME_COMMAND_KEY_RELEASE,
  CME_COMMAND_SCROLL,
  CME_COMMAND_SCROLL_TO,
  CME_COMMAND_GET_CAN_GO_BACK,
  CME_COMMAND_GET_CAN_GO_FORWARD,
  CME_COMMAND_GET_SHM_NAME,
  CME_COMMAND_BACK,
  CME_COMMAND_FORWARD,
  CME_COMMAND_STOP,
  CME_COMMAND_REFRESH,
  CME_COMMAND_RELOAD,
  CME_COMMAND_SET_CHROME,
  CME_COMMAND_TOGGLE_CHROME,
  CME_COMMAND_QUIT,
  CME_COMMAND_NEW_VIEW,
  CME_COMMAND_NEW_WINDOW,
  CME_COMMAND_NEW_WINDOW_RESPONSE,
  CME_COMMAND_NEW_WINDOW_CANCEL,
  CME_COMMAND_FOCUS,
  CME_COMMAND_PLUGIN_WINDOW,
  CME_COMMAND_MAP
} ClutterMozEmbedCommand;

void clutter_mozembed_comms_sendv (GIOChannel *channel, gint command_id, va_list args);
void clutter_mozembed_comms_send (GIOChannel *channel, gint command_id, ...);
gboolean clutter_mozembed_comms_receive (GIOChannel *channel, ...);

/* Convenience functions to read out a single variable at a time */
gchar *clutter_mozembed_comms_receive_string (GIOChannel *channel);
gboolean clutter_mozembed_comms_receive_boolean (GIOChannel *channel);
gint clutter_mozembed_comms_receive_int (GIOChannel *channel);
guint clutter_mozembed_comms_receive_uint (GIOChannel *channel);
glong clutter_mozembed_comms_receive_long (GIOChannel *channel);
gulong clutter_mozembed_comms_receive_ulong (GIOChannel *channel);
gdouble clutter_mozembed_comms_receive_double (GIOChannel *channel);

#endif /* _CLUTTER_MOZEMBED_COMMS */

