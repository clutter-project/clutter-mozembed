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

#include "clutter-mozembed-comms.h"
#include <glib-object.h>
#include <string.h>

void
clutter_mozembed_comms_sendv (GIOChannel *channel, gint command_id, va_list args)
{
  GType type;

  /* FIXME: Add error handling */
  /*g_debug ("Sending command: %d", command_id);*/

  if (!channel)
    {
      g_warning ("Trying to send command %d with NULL channel", command_id);
      return;
    }

  g_io_channel_write_chars (channel,
                            (gchar *)(&command_id),
                            sizeof (command_id),
                            NULL,
                            NULL);

  while ((type = va_arg (args, GType)) != G_TYPE_INVALID)
    {
      gint int_val;
      glong long_val;
      gint64 int64_val;
      gdouble double_val;
      const gchar *buffer = NULL;
      gssize size = 0;

      switch (type)
        {
        case G_TYPE_UINT :
        case G_TYPE_INT :
        case G_TYPE_BOOLEAN :
          int_val = va_arg (args, gint);
          size = sizeof (gint);
          buffer = (const gchar *)(&int_val);
          break;

        case G_TYPE_ULONG :
        case G_TYPE_LONG :
          long_val = va_arg (args, glong);
          size = sizeof (glong);
          buffer = (const gchar *)(&long_val);
          break;

        case G_TYPE_UINT64 :
        case G_TYPE_INT64 :
          int64_val = va_arg (args, gint64);
          size = sizeof (gint64);
          buffer = (const gchar *)(&int64_val);
          break;

        case G_TYPE_DOUBLE :
          double_val = va_arg (args, gdouble);
          size = sizeof (gdouble);
          buffer = (const gchar *)(&double_val);
          break;

        case G_TYPE_STRING :
          buffer = va_arg (args, gpointer);
          if (buffer)
            size = strlen (buffer) + 1;
          else
            size = 0;
          g_io_channel_write_chars (channel,
                                    (gchar *)(&size),
                                    sizeof (size),
                                    NULL,
                                    NULL);
          break;

        case G_TYPE_NONE:
          size = va_arg (args, gsize);
          buffer = (const gchar *)va_arg (args, gpointer);
          break;

        default:
          g_warning ("Trying to send unknown type (command %d)", command_id);
        }

      if (!buffer)
        {
          g_warning ("No data to send (command %d)", command_id);
          continue;
        }

      if (size)
        g_io_channel_write_chars (channel,
                                  buffer,
                                  size,
                                  NULL,
                                  NULL);
    }

  g_io_channel_flush (channel, NULL);
}

void
clutter_mozembed_comms_send (GIOChannel *channel, gint command_id, ...)
{
  va_list args;

  va_start (args, command_id);

  clutter_mozembed_comms_sendv (channel, command_id, args);

  va_end (args);
}

gboolean
clutter_mozembed_comms_receive (GIOChannel *channel, ...)
{
  GType type;
  va_list args;

  gboolean success = TRUE;

  if (!channel)
    {
      g_warning ("Trying to receive data with NULL channel");
      return FALSE;
    }

  va_start (args, channel);

  while ((type = va_arg (args, GType)) != G_TYPE_INVALID)
    {
      GIOStatus status;
      gsize size;

      gchar *buffer = va_arg (args, gpointer);
      GError *error = NULL;

      switch (type)
        {
        case G_TYPE_UINT :
        case G_TYPE_INT :
        case G_TYPE_BOOLEAN :
          size = sizeof (gint);
          break;

        case G_TYPE_LONG :
        case G_TYPE_ULONG :
          size = sizeof (glong);
          break;

        case G_TYPE_UINT64 :
        case G_TYPE_INT64 :
          size = sizeof (gint64);
          break;

        case G_TYPE_DOUBLE :
          size = sizeof (gdouble);
          break;

        case G_TYPE_STRING :
          size = -1;
          break;

        case G_TYPE_NONE:
          size = va_arg (args, gsize);
          break;

        default:
          g_warning ("Trying to receive unknown type, trying to continue");
          va_arg (args, gpointer);
          continue;
        }

      if (size == -1)
        {
          do
            {
              status = g_io_channel_read_chars (channel,
                                                (gchar *)(&size),
                                                sizeof (size),
                                                NULL,
                                                &error);
            } while (status == G_IO_STATUS_AGAIN);

          if (size)
            {
              *((gchar **)buffer) = g_malloc (size);

              do
                {
                  status = g_io_channel_read_chars (channel,
                                                    *((gchar **)buffer),
                                                    size,
                                                    NULL,
                                                    &error);
                } while (status == G_IO_STATUS_AGAIN);
            } else
              *((gchar **)buffer) = NULL;
        }
      else do
        {
          status = g_io_channel_read_chars (channel,
                                            buffer,
                                            size,
                                            NULL,
                                            &error);
        } while (status == G_IO_STATUS_AGAIN);

      if (status != G_IO_STATUS_NORMAL)
        {
          success = FALSE;

          if (error)
            {
              g_warning ("Error reading from pipe: %s", error->message);
              g_error_free (error);
            }

          break;
        }
    }

  va_end (args);

  return success;
}

gchar *
clutter_mozembed_comms_receive_string (GIOChannel *channel)
{
  gchar *returnval = NULL;
  clutter_mozembed_comms_receive (channel, G_TYPE_STRING, &returnval,
                                  G_TYPE_INVALID);
  return returnval;
}

gboolean
clutter_mozembed_comms_receive_boolean (GIOChannel *channel)
{
  gboolean returnval = FALSE;
  clutter_mozembed_comms_receive (channel, G_TYPE_BOOLEAN, &returnval,
                                  G_TYPE_INVALID);
  return returnval;
}

gint
clutter_mozembed_comms_receive_int (GIOChannel *channel)
{
  gint returnval = 0;
  clutter_mozembed_comms_receive (channel, G_TYPE_INT, &returnval,
                                  G_TYPE_INVALID);
  return returnval;
}

guint
clutter_mozembed_comms_receive_uint (GIOChannel *channel)
{
  guint returnval = 0;
  clutter_mozembed_comms_receive (channel, G_TYPE_UINT, &returnval,
                                  G_TYPE_INVALID);
  return returnval;
}

glong
clutter_mozembed_comms_receive_long (GIOChannel *channel)
{
  glong returnval = 0;
  clutter_mozembed_comms_receive (channel, G_TYPE_LONG, &returnval,
                                  G_TYPE_INVALID);
  return returnval;
}

gulong
clutter_mozembed_comms_receive_ulong (GIOChannel *channel)
{
  gulong returnval = 0;
  clutter_mozembed_comms_receive (channel, G_TYPE_ULONG, &returnval,
                                  G_TYPE_INVALID);
  return returnval;
}

gdouble
clutter_mozembed_comms_receive_double (GIOChannel *channel)
{
  gdouble returnval = 0.0;
  clutter_mozembed_comms_receive (channel, G_TYPE_DOUBLE, &returnval,
                                  G_TYPE_INVALID);
  return returnval;
}

