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

#include "clutter-mozembed-download.h"

#include "clutter-mozembed.h"
#include "clutter-mozembed-comms.h"
#include "clutter-mozembed-private.h"

G_DEFINE_TYPE (ClutterMozEmbedDownload, clutter_mozembed_download, G_TYPE_OBJECT)

#define DOWNLOAD_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), CLUTTER_TYPE_MOZEMBED_DOWNLOAD, \
                                ClutterMozEmbedDownloadPrivate))

enum
{
  PROP_0,

  PROP_PARENT,
  PROP_ID,
  PROP_SOURCE,
  PROP_DESTINATION,
  PROP_PROGRESS,
  PROP_MAX_PROGRESS,
  PROP_COMPLETE,
  PROP_CANCELLED
};

struct _ClutterMozEmbedDownloadPrivate
{
  ClutterMozEmbed *parent;
  gint             id;
  gchar           *source_uri;
  gchar           *dest_uri;
  gint64           progress;
  gint64           max_progress;
  gboolean         complete;
  gboolean         cancelled;
};

static void
clutter_mozembed_download_get_property (GObject *object, guint property_id,
                                        GValue *value, GParamSpec *pspec)
{
  ClutterMozEmbedDownload *self = CLUTTER_MOZEMBED_DOWNLOAD (object);

  switch (property_id) {
  case PROP_PARENT :
    g_value_set_object (value, self->priv->parent);
    break;

  case PROP_ID :
    g_value_set_int (value, self->priv->id);
    break;

  case PROP_SOURCE :
    g_value_set_string (value, clutter_mozembed_download_get_source (self));
    break;

  case PROP_DESTINATION :
    g_value_set_string (value,
                        clutter_mozembed_download_get_destination (self));
    break;

  case PROP_PROGRESS :
    g_value_set_int64 (value, clutter_mozembed_download_get_progress (self));
    break;

  case PROP_MAX_PROGRESS :
    g_value_set_int64 (value, clutter_mozembed_download_get_max_progress (self));
    break;

  case PROP_COMPLETE :
    g_value_set_boolean (value, clutter_mozembed_download_get_complete (self));
    break;

  case PROP_CANCELLED :
    g_value_set_boolean (value, clutter_mozembed_download_get_cancelled (self));
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
clutter_mozembed_download_set_property (GObject *object, guint property_id,
                                        const GValue *value, GParamSpec *pspec)
{
  ClutterMozEmbedDownload *self = CLUTTER_MOZEMBED_DOWNLOAD (object);
  ClutterMozEmbedDownloadPrivate *priv = self->priv;

  switch (property_id) {
  case PROP_PARENT :
    priv->parent = g_value_get_object (value);
    break;

  case PROP_ID :
    priv->id = g_value_get_int (value);
    break;

  case PROP_SOURCE :
    priv->source_uri = g_value_dup_string (value);
    break;

  case PROP_DESTINATION :
    priv->dest_uri = g_value_dup_string (value);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
clutter_mozembed_download_dispose (GObject *object)
{
  /*ClutterMozEmbedDownload *self = CLUTTER_MOZEMBED_DOWNLOAD (object);
  ClutterMozEmbedDownloadPrivate *priv = self->priv;*/

  G_OBJECT_CLASS (clutter_mozembed_download_parent_class)->finalize (object);
}

static void
clutter_mozembed_download_finalize (GObject *object)
{
  ClutterMozEmbedDownloadPrivate *priv =
    CLUTTER_MOZEMBED_DOWNLOAD (object)->priv;

  g_free (priv->source_uri);
  g_free (priv->dest_uri);
}

static void
clutter_mozembed_download_class_init (ClutterMozEmbedDownloadClass *klass)
{
  GObjectClass      *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (ClutterMozEmbedDownloadPrivate));

  object_class->get_property = clutter_mozembed_download_get_property;
  object_class->set_property = clutter_mozembed_download_set_property;
  object_class->dispose = clutter_mozembed_download_dispose;
  object_class->finalize = clutter_mozembed_download_finalize;

  g_object_class_install_property (object_class,
                                   PROP_PARENT,
                                   g_param_spec_object ("parent",
                                                        "Parent",
                                                        "Parent ClutterMozEmbed.",
                                                        CLUTTER_TYPE_MOZEMBED,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_ID,
                                   g_param_spec_int ("id",
                                                     "ID",
                                                     "Unique download ID.",
                                                     G_MININT, G_MAXINT, 0,
                                                     G_PARAM_READWRITE |
                                                     G_PARAM_CONSTRUCT_ONLY |
                                                     G_PARAM_STATIC_NAME |
                                                     G_PARAM_STATIC_NICK |
                                                     G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_SOURCE,
                                   g_param_spec_string ("source",
                                                        "Source URI",
                                                        "Download source URI.",
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_DESTINATION,
                                   g_param_spec_string ("destination",
                                                        "Destination URI",
                                                        "Download destination "
                                                        "URI.",
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_PROGRESS,
                                   g_param_spec_int64 ("progress",
                                                       "Progress",
                                                       "Download progress.",
                                                       -1, G_MAXINT64, 0,
                                                       G_PARAM_READABLE |
                                                       G_PARAM_STATIC_NAME |
                                                       G_PARAM_STATIC_NICK |
                                                       G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_MAX_PROGRESS,
                                   g_param_spec_int64 ("max-progress",
                                                       "Max progress",
                                                       "Maximum download "
                                                       "progress.",
                                                       -1, G_MAXINT64, 0,
                                                       G_PARAM_READABLE |
                                                       G_PARAM_STATIC_NAME |
                                                       G_PARAM_STATIC_NICK |
                                                       G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_COMPLETE,
                                   g_param_spec_boolean ("complete",
                                                         "Complete",
                                                         "Download complete.",
                                                         FALSE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_NICK |
                                                         G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_CANCELLED,
                                   g_param_spec_boolean ("cancelled",
                                                         "Cancelled",
                                                         "Download cancelled.",
                                                         FALSE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_NICK |
                                                         G_PARAM_STATIC_BLURB));
}

static void
clutter_mozembed_download_init (ClutterMozEmbedDownload *self)
{
  /*ClutterMozEmbedDownloadPrivate *priv = */
  self->priv = DOWNLOAD_PRIVATE (self);
}

ClutterMozEmbedDownload *
clutter_mozembed_download_new (ClutterMozEmbed *parent,
                               gint             id,
                               const gchar     *source,
                               const gchar     *destination)
{
  return g_object_new (CLUTTER_TYPE_MOZEMBED_DOWNLOAD,
                       "parent", parent,
                       "id", id,
                       "source", source,
                       "destination", destination,
                       NULL);
}

const gchar *
clutter_mozembed_download_get_source (ClutterMozEmbedDownload *self)
{
  return self->priv->source_uri;
}

const gchar *
clutter_mozembed_download_get_destination (ClutterMozEmbedDownload *self)
{
  return self->priv->dest_uri;
}

gint64
clutter_mozembed_download_get_progress (ClutterMozEmbedDownload *self)
{
  return self->priv->progress;
}

gint64
clutter_mozembed_download_get_max_progress (ClutterMozEmbedDownload *self)
{
  return self->priv->max_progress;
}

void
clutter_mozembed_download_set_progress (ClutterMozEmbedDownload *self,
                                        gint64                   progress,
                                        gint64                   max_progress)
{
  ClutterMozEmbedDownloadPrivate *priv = self->priv;

  if (priv->progress != progress)
    {
      priv->progress = progress;
      g_object_notify (G_OBJECT (self), "progress");
    }

  if (priv->max_progress != max_progress)
    {
      priv->max_progress = max_progress;
      g_object_notify (G_OBJECT (self), "max-progress");
    }
}

void
clutter_mozembed_download_set_complete (ClutterMozEmbedDownload *self,
                                        gboolean                 complete)
{
  ClutterMozEmbedDownloadPrivate *priv = self->priv;

  if (priv->complete != complete)
    {
      priv->complete = complete;
      g_object_notify (G_OBJECT (self), "complete");
    }
}

gboolean
clutter_mozembed_download_get_complete (ClutterMozEmbedDownload *self)
{
  return self->priv->complete;
}

void
clutter_mozembed_download_set_cancelled (ClutterMozEmbedDownload *self)
{
  ClutterMozEmbedDownloadPrivate *priv = self->priv;

  if (!priv->cancelled)
    {
      priv->cancelled = TRUE;
      g_object_notify (G_OBJECT (self), "cancelled");
    }
}

gboolean
clutter_mozembed_download_get_cancelled (ClutterMozEmbedDownload *self)
{
  return self->priv->cancelled;
}

void
clutter_mozembed_download_cancel (ClutterMozEmbedDownload *self)
{
  ClutterMozEmbedPrivate *priv = self->priv->parent->priv;

  clutter_mozembed_comms_send (priv->output,
                               CME_COMMAND_DL_CANCEL,
                               G_TYPE_INT, self->priv->id,
                               G_TYPE_INVALID);
}

