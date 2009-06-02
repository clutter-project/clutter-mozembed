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
#include "clutter-mozembed-private.h"

G_DEFINE_TYPE (ClutterMozEmbedDownload, clutter_mozembed_download, G_TYPE_OBJECT)

#define DOWNLOAD_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), CLUTTER_TYPE_MOZEMBED, \
                                ClutterMozEmbedDownloadPrivate))

enum
{
  PROP_0,

  PROP_SOURCE,
  PROP_DESTINATION,
  PROP_PROGRESS,
};

enum
{
  COMPLETE,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

struct _ClutterMozEmbedDownloadPrivate
{
  gchar   *source_uri;
  gchar   *dest_uri;
  gdouble  progress;
};

static void
clutter_mozembed_download_get_property (GObject *object, guint property_id,
                                        GValue *value, GParamSpec *pspec)
{
  ClutterMozEmbedDownload *self = CLUTTER_MOZEMBED_DOWNLOAD (object);

  switch (property_id) {
  case PROP_SOURCE :
    g_value_set_string (value, clutter_mozembed_download_get_source (self));
    break;

  case PROP_DESTINATION :
    g_value_set_string (value,
                        clutter_mozembed_download_get_destination (self));
    break;

  case PROP_PROGRESS :
    g_value_set_double (value, clutter_mozembed_download_get_progress (self));
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
                                   g_param_spec_double ("progress",
                                                        "Progress",
                                                        "Download progress.",
                                                        0.0, 100.0, 0.0,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  signals[COMPLETE] =
    g_signal_new ("complete",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterMozEmbedDownloadClass, complete),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}

static void
clutter_mozembed_download_init (ClutterMozEmbedDownload *self)
{
  /*ClutterMozEmbedDownloadPrivate *priv = */
  self->priv = DOWNLOAD_PRIVATE (self);
}

ClutterMozEmbedDownload *
clutter_mozembed_download_new (const gchar *source, const gchar *destination)
{
  return g_object_new (CLUTTER_TYPE_MOZEMBED_DOWNLOAD,
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

gdouble
clutter_mozembed_download_get_progress (ClutterMozEmbedDownload *self)
{
  return self->priv->progress;
}

void
clutter_mozembed_download_set_progress (ClutterMozEmbedDownload *self,
                                        gdouble                  progress)
{
  ClutterMozEmbedDownloadPrivate *priv = self->priv;

  if (priv->progress != progress)
    {
      priv->progress = progress;
      g_object_notify (G_OBJECT (self), "progress");
    }
}

