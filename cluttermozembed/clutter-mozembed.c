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

#include "clutter-mozembed.h"
#include <moz-headless.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

G_DEFINE_TYPE (ClutterMozEmbed, clutter_mozembed, CLUTTER_TYPE_ACTOR)

#define MOZEMBED_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), CLUTTER_TYPE_MOZEMBED, ClutterMozEmbedPrivate))

enum
{
  PROP_0,
 
  PROP_LOCATION,
  PROP_TITLE,
};

enum
{
  PROGRESS,
  NET_START,
  NET_STOP,
  
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

struct _ClutterMozEmbedPrivate
{
  GIOChannel      *input;
  FILE            *output;
  
  gboolean         motion_ack;
  gulong           motion_throttle;
  gint             motion_x;
  gint             motion_y;
  
  ClutterActor    *textures[4];
  
  /* Variables for synchronous calls */
  const gchar     *sync_call;
  gboolean         can_go_back;
  gboolean         can_go_forward;
  
  /* Locally cached properties */
  gchar           *location;
  gchar           *title;
  
  /* Scroll coordinates for local textures */
  gint             sx;
  gint             sy;
  
  gchar           *shm_name;
  gboolean         opened_shm;
  gboolean         new_data;
  int              shm_fd;
  
  /* Scroll coordinates for the back-end */
  gint             hsx;
  gint             hsy;
  
  void            *image_data;
  int              image_size;
};

static gboolean
separate_strings (gchar **strings, gint n_strings, gchar *string)
{
  gint i;
  gboolean success = TRUE;
  
  strings[0] = string;
  for (i = 0; i < n_strings - 1; i++)
    {
      gchar *string = strchr (strings[i], ' ');
      if (!string)
        {
          success = FALSE;
          break;
        }
      string[0] = '\0';
      strings[i+1] = string + 1;
    }
  
  return success;
}

static void
update (ClutterMozEmbed *self, gint x, gint y, gint width, gint height)
{
  ClutterMozEmbedPrivate *priv = self->priv;
  guint surface_width, surface_height, i;
  
  if (!priv->opened_shm)
    {
      g_debug ("Opening shm");
      priv->opened_shm = TRUE;
      priv->shm_fd = shm_open (priv->shm_name, O_RDONLY, 0444);
      if (priv->shm_fd == -1)
        g_error ("Error opening shared memory");
    }
  
  clutter_actor_get_size (CLUTTER_ACTOR (self),
                          &surface_width, &surface_height);
  
  if (!priv->image_data)
    {
      g_debug ("mmapping data");
      priv->image_size = surface_width * surface_height * 4;
      priv->image_data = mmap (NULL, priv->image_size, PROT_READ,
                               MAP_SHARED, priv->shm_fd, 0);
    }
  
  /*g_debug ("Reading data");*/
  for (i = 0; i < 4; i++)
    {
      gint dx, dy, dw, dh;
      GError *error = NULL;
      
      if (!priv->textures[i])
        continue;
      
      dw = surface_width;
      dh = surface_height;
      
      /* Calculate the rectangle of the texture patch */
      switch (i)
        {
        /* top-left */
        case 0:
          dx = priv->sx;
          dy = priv->sy;
          break;
        
        /* top-right */
        case 1:
          dx = surface_width + priv->sx;
          dy = priv->sy;
          break;
        
        /* bottom-left */
        case 2:
          dx = priv->sx;
          dy = surface_height + priv->sy;
          break;
        
        /* bottom-right */
        case 3:
          dx = surface_width + priv->sx;
          dy = surface_height + priv->sy;
          break;
        }
      
      /* If the rectangle intersects with the updated rectangle, copy the
       * intersection to the texture.
       */
      if ((dx < x + width) &&
          (dy < y + height) &&
          (dx + dw > x) &&
          (dy + dh > y))
        {
          gint src_x, src_y, dst_x, dst_y, dst_width, dst_height;
          
          if (dx >= x)
            {
              src_x = x - dx;
              dst_x = 0;
              dst_width = MIN (dw, (x + width) - dx);
            }
          else
            {
              src_x = 0;
              dst_x = x - dx;
              dst_width = MIN (dw - dst_x, width);
            }
          if (dy >= y)
            {
              src_y = y - dy;
              dst_y = 0;
              dst_height = MIN (dh, (y + height) - dy);
            }
          else
            {
              src_y = 0;
              dst_y = y - dy;
              dst_height = MIN (dh - dst_y, height);
            }
          
          /*g_debug ("Copying from %d,%d (%dx%d) to %d,%d %dx%d (%d)",
                   src_x, src_y, surface_width, surface_height,
                   dst_x, dst_y, dst_width, dst_height, i);*/
          
          if (!clutter_texture_set_area_from_rgb_data (
                                           CLUTTER_TEXTURE (priv->textures[i]),
                                           priv->image_data +
                                           ((src_x+x)*4) +
                                           ((src_y+y)*surface_width*4),
                                           TRUE,
                                           dst_x,
                                           dst_y,
                                           dst_width,
                                           dst_height,
                                           surface_width*4,
                                           4,
                                           CLUTTER_TEXTURE_RGB_FLAG_BGR,
                                           &error))
            {
              g_warning ("Error setting texture data: %s", error->message);
              g_error_free (error);
            }
        }
    }
}

static void
process_feedback (ClutterMozEmbed *self, const gchar *command)
{
  gchar *detail;
  
  ClutterMozEmbedPrivate *priv = self->priv;
  
  /*g_debug ("Processing feedback: %s", command);*/
  
  detail = strchr (command, ' ');
  if (detail)
    {
      detail[0] = '\0';
      detail++;
    }
  
  if (priv->sync_call && g_str_equal (command, priv->sync_call))
    priv->sync_call = NULL;

  if (g_str_equal (command, "update"))
    {
      gint x, y, width, height;
      
      gchar *params[4];
      if (!separate_strings (params, G_N_ELEMENTS (params), detail))
        return;
      
      x = atoi (params[0]);
      y = atoi (params[1]);
      width = atoi (params[2]);
      height = atoi (params[3]);
      
      priv->new_data = TRUE;
      
      update (self, x, y, width, height);
      
      clutter_actor_queue_redraw (CLUTTER_ACTOR (self));
    }
  else if (g_str_equal (command, "mack"))
    {
      priv->motion_ack = TRUE;
    }
  else if (g_str_equal (command, "progress"))
    {
      gdouble progress;
      
      gchar *params[1];

      if (!separate_strings (params, G_N_ELEMENTS (params), detail))
        return;
      
      progress = atof (params[0]);
      
      g_signal_emit (self, signals[PROGRESS], 0, progress);
    }
  else if (g_str_equal (command, "location"))
    {
      g_free (priv->location);
      priv->location = NULL;
      
      if (!detail)
        return;
      
      priv->location = g_strdup (detail);
      g_object_notify (G_OBJECT (self), "location");
    }
  else if (g_str_equal (command, "title"))
    {
      g_free (priv->title);
      priv->title = NULL;
      
      if (!detail)
        return;
      
      priv->title = g_strdup (detail);
      g_object_notify (G_OBJECT (self), "title");
    }
  else if (g_str_equal (command, "net-start"))
    {
      g_signal_emit (self, signals[NET_START], 0);
    }
  else if (g_str_equal (command, "net-stop"))
    {
      g_signal_emit (self, signals[NET_STOP], 0);
    }
  else if (g_str_equal (command, "back"))
    {
      gchar *params[1];
      if (!separate_strings (params, G_N_ELEMENTS (params), detail))
        return;
      priv->can_go_back = atoi (params[0]);
    }
  else if (g_str_equal (command, "forward"))
    {
      gchar *params[1];
      if (!separate_strings (params, G_N_ELEMENTS (params), detail))
        return;
      priv->can_go_forward = atoi (params[0]);
    }
  else
    {
      g_warning ("Unrecognised feedback: %s", command);
    }
}

static gboolean
input_io_func (GIOChannel      *source,
               GIOCondition     condition,
               ClutterMozEmbed *self)
{
  /* FYI: Maximum URL length in IE is 2083 characters */
  gchar buf[4096];
  gsize length;

  GError *error = NULL;
  
  switch (condition) {
    case G_IO_PRI :
    case G_IO_IN :
      if (g_io_channel_read_chars (source, buf, sizeof (buf), &length, &error)
          == G_IO_STATUS_NORMAL) {
        gsize current_length = 0;
        while (current_length < length)
          {
            gchar *feedback = &buf[current_length];
            current_length += strlen (&buf[current_length]) + 1;
            process_feedback (self, feedback);
          }
      } else {
        g_warning ("Error reading from source: %s", error->message);
        g_error_free (error);
      }
      break;

    case G_IO_ERR :
    case G_IO_NVAL :
      g_warning ("Error or invalid request");
      return FALSE;
    
    case G_IO_HUP :
      g_warning ("Hung up");
      return FALSE;
    
    default :
      g_warning ("Unhandled IO condition");
      return FALSE;
  }

  return TRUE;
}

static void
send_command (ClutterMozEmbed *mozembed, const gchar *command)
{
  if (!mozembed->priv->output) {
    g_warning ("Child process is not available");
    return;
  }
  
  /*g_debug ("Sending command: %s", command);*/
  
  fwrite (command, strlen (command) + 1, 1, mozembed->priv->output);
  fflush (mozembed->priv->output);
}

static void
block_until_feedback (ClutterMozEmbed *mozembed, const gchar *feedback)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;
  
  priv->sync_call = feedback;
  
  /* FIXME: There needs to be a time limit here, or we can hang if the backend
   *        hangs. Here or in input_io_func anyway...
   */
  while (input_io_func (priv->input, G_IO_IN, mozembed) && priv->sync_call);
  
  if (priv->sync_call)
    g_warning ("Error making synchronous call to backend");
}

static void
clutter_mozembed_get_property (GObject *object, guint property_id,
                              GValue *value, GParamSpec *pspec)
{
  ClutterMozEmbed *self = CLUTTER_MOZEMBED (object);
  
  switch (property_id) {
  case PROP_LOCATION :
    g_value_set_string (value, clutter_mozembed_get_location (self));
    break;

  case PROP_TITLE :
    g_value_set_string (value, clutter_mozembed_get_title (self));
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
clutter_mozembed_set_property (GObject *object, guint property_id,
                              const GValue *value, GParamSpec *pspec)
{
  switch (property_id) {
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
clutter_mozembed_dispose (GObject *object)
{
  ClutterMozEmbedPrivate *priv = CLUTTER_MOZEMBED (object)->priv;
  
  if (priv->input)
    {
      GError *error = NULL;
      
      if (g_io_channel_shutdown (priv->input, FALSE, &error) ==
          G_IO_STATUS_ERROR)
        {
          g_warning ("Error closing IO channel: %s", error->message);
          g_error_free (error);
        }
      
      g_io_channel_unref (priv->input);
      priv->input = NULL;
    }
  
  if (priv->output)
    {
      fclose (priv->output);
      priv->output = NULL;
    }
  
  G_OBJECT_CLASS (clutter_mozembed_parent_class)->dispose (object);
}

static void
clutter_mozembed_finalize (GObject *object)
{
  ClutterMozEmbedPrivate *priv = CLUTTER_MOZEMBED (object)->priv;
  
  if (priv->shm_name)
    g_free (priv->shm_name);
  
  if (priv->image_data)
    munmap (priv->image_data, priv->image_size);
  
  G_OBJECT_CLASS (clutter_mozembed_parent_class)->finalize (object);
}

static void
clutter_mozembed_allocate (ClutterActor          *actor,
                           const ClutterActorBox *box,
                           gboolean               absolute_origin_changed)
{
  gchar *command;
  ClutterActorBox child_box;
  gint width, height, tex_width, tex_height, i;
  ClutterMozEmbedPrivate *priv = CLUTTER_MOZEMBED (actor)->priv;

  width = CLUTTER_UNITS_TO_INT (box->x2 - box->x1);
  height = CLUTTER_UNITS_TO_INT (box->y2 - box->y1);
  
  if (priv->textures[0])
    clutter_texture_get_base_size (CLUTTER_TEXTURE (priv->textures[0]),
                                   &tex_width, &tex_height);
  
  if ((!priv->textures[0]) || (tex_width != width) || (tex_height != height))
    {
      const guchar *data = g_malloc (width * height * 4);
      for (i = 0; i < 4; i++)
        {
          if (priv->textures[i])
            clutter_actor_unparent (priv->textures[i]);
          priv->textures[i] =
            g_object_new (CLUTTER_TYPE_TEXTURE, "visible", TRUE, NULL);
          clutter_texture_set_from_rgb_data (CLUTTER_TEXTURE (priv->textures[i]),
                                             data, TRUE, width, height,
                                             width * 4, 4, 0, NULL);
          clutter_actor_set_parent (priv->textures[i], actor);
        }
    }
  
  child_box.x1 = 0;
  child_box.y1 = 0;
  child_box.x2 = box->x2 - box->x1;
  child_box.y2 = box->y2 - box->y1;
  for (i = 0; i < 4; i++)
    clutter_actor_allocate (priv->textures[i], &child_box,
                            absolute_origin_changed);
  
  if (priv->image_data)
    {
      munmap (priv->image_data, priv->image_size);
      priv->image_data = NULL;
    }
  
  command = g_strdup_printf ("resize %d %d", width, height);
  send_command (CLUTTER_MOZEMBED (actor), command);
  g_free (command);
  
  CLUTTER_ACTOR_CLASS (clutter_mozembed_parent_class)->
    allocate (actor, box, absolute_origin_changed);
}

static void
clutter_mozembed_paint (ClutterActor *actor)
{
  ClutterMozEmbed *self = CLUTTER_MOZEMBED (actor);
  ClutterMozEmbedPrivate *priv = self->priv;
  
  if (priv->textures[0])
    {
      gint width = cogl_texture_get_width (priv->textures[0]);
      gint height = cogl_texture_get_height (priv->textures[0]);
      ClutterFixed widthx = CLUTTER_INT_TO_FIXED (width);
      ClutterFixed heightx = CLUTTER_INT_TO_FIXED (height);

      cogl_clip_set (0, 0, widthx, heightx);
      cogl_translate (priv->sx, priv->sy, 0);
      
      clutter_actor_paint (priv->textures[0]);
      cogl_translate (width, 0, 0);
      clutter_actor_paint (priv->textures[1]);
      cogl_translate (-width, height, 0);
      clutter_actor_paint (priv->textures[2]);
      cogl_translate (width, 0, 0);
      clutter_actor_paint (priv->textures[3]);

      cogl_clip_unset ();
    }
  
  if (priv->new_data)
    {
      priv->new_data = FALSE;
      send_command (self, "ack");
    }
}

static void
clutter_mozembed_pick (ClutterActor *actor, const ClutterColor *color)
{
  guint width, height;
  
  cogl_color (color);
  clutter_actor_get_size (actor, &width, &height);
  cogl_rectangle (0, 0, width, height);
}

static gboolean
motion_idle (ClutterMozEmbed *self)
{
  gchar *command;
  ClutterMozEmbedPrivate *priv = self->priv;

  if (!priv->motion_ack)
    return TRUE;

  command = g_strdup_printf ("motion %d %d", priv->motion_x, priv->motion_y);
  send_command (self, command);
  g_free (command);
  
  priv->motion_throttle = 0;

  return FALSE;
}

static gboolean
clutter_mozembed_motion_event (ClutterActor *actor, ClutterMotionEvent *event)
{
  ClutterMozEmbedPrivate *priv;
  ClutterUnit x_out, y_out;
  
  if (!clutter_actor_transform_stage_point (actor,
                                            CLUTTER_UNITS_FROM_INT (event->x),
                                            CLUTTER_UNITS_FROM_INT (event->y),
                                            &x_out, &y_out))
    return FALSE;
  
  priv = CLUTTER_MOZEMBED (actor)->priv;
  
  priv->motion_x = CLUTTER_UNITS_TO_INT (x_out);
  priv->motion_y = CLUTTER_UNITS_TO_INT (y_out);
  
  /* Throttle motion events while there's new data waiting, otherwise we can
   * peg the back-end rendering new frames. (back-end sends 'mack' when it
   * finishes processing a motion event)
   */
  if (!priv->motion_ack)
    {
      if (!priv->motion_throttle)
        priv->motion_throttle = g_idle_add ((GSourceFunc)motion_idle, actor);

      return TRUE;
    }
  
  if (priv->motion_throttle)
    {
      g_source_remove (priv->motion_throttle);
      priv->motion_throttle = 0;
    }
  
  motion_idle (CLUTTER_MOZEMBED (actor));
  
  priv->motion_ack = FALSE;
  
  return TRUE;
}

static gboolean
clutter_mozembed_button_press_event (ClutterActor *actor,
                                     ClutterButtonEvent *event)
{
  ClutterMozEmbedPrivate *priv;
  ClutterUnit x_out, y_out;
  gchar *command;
  
  if (!clutter_actor_transform_stage_point (actor,
                                            CLUTTER_UNITS_FROM_INT (event->x),
                                            CLUTTER_UNITS_FROM_INT (event->y),
                                            &x_out, &y_out))
    return FALSE;
  
  clutter_grab_pointer (actor);
  
  priv = CLUTTER_MOZEMBED (actor)->priv;
  command = g_strdup_printf ("button-press %d %d %d %d",
                             CLUTTER_UNITS_TO_INT (x_out),
                             CLUTTER_UNITS_TO_INT (y_out),
                             event->button,
                             event->click_count);
  send_command (CLUTTER_MOZEMBED (actor), command);
  g_free (command);
  
  return TRUE;
}

static gboolean
clutter_mozembed_button_release_event (ClutterActor *actor,
                                       ClutterButtonEvent *event)
{
  ClutterMozEmbedPrivate *priv;
  ClutterUnit x_out, y_out;
  gchar *command;
  
  clutter_ungrab_pointer ();
  
  if (!clutter_actor_transform_stage_point (actor,
                                            CLUTTER_UNITS_FROM_INT (event->x),
                                            CLUTTER_UNITS_FROM_INT (event->y),
                                            &x_out, &y_out))
    return FALSE;
  
  priv = CLUTTER_MOZEMBED (actor)->priv;
  command = g_strdup_printf ("button-release %d %d %d",
                             CLUTTER_UNITS_TO_INT (x_out),
                             CLUTTER_UNITS_TO_INT (y_out),
                             event->button);
  send_command (CLUTTER_MOZEMBED (actor), command);
  g_free (command);
  
  return TRUE;
}

static gboolean
clutter_mozembed_get_keyval (ClutterKeyEvent *event, guint *keyval)
{
  *keyval = event->unicode_value;
  
  if (g_unichar_isprint (*keyval))
    return TRUE;

  switch (event->keyval)
    {
    case CLUTTER_Return :
      *keyval = MOZ_KEY_RETURN;
      break;
    case CLUTTER_BackSpace :
      *keyval = MOZ_KEY_BACK_SPACE;
      break;
    default :
      return FALSE;
    }
  
  return TRUE;
}

static gboolean
clutter_mozembed_key_press_event (ClutterActor *actor, ClutterKeyEvent *event)
{
  guint keyval;
  
  if (clutter_mozembed_get_keyval (event, &keyval))
    {
      gchar *command = g_strdup_printf ("key-press %d", keyval);
      send_command (CLUTTER_MOZEMBED (actor), command);
      g_free (command);
      return TRUE;
    }
  
  return FALSE;
}

static gboolean
clutter_mozembed_key_release_event (ClutterActor *actor, ClutterKeyEvent *event)
{
  guint keyval;
  
  if (clutter_mozembed_get_keyval (event, &keyval))
    {
      gchar *command = g_strdup_printf ("key-release %d", keyval);
      send_command (CLUTTER_MOZEMBED (actor), command);
      g_free (command);
      return TRUE;
    }
  
  return FALSE;
}

static gboolean
clutter_mozembed_scroll_event (ClutterActor *actor,
                               ClutterScrollEvent *event)
{
  ClutterMozEmbedPrivate *priv;
  ClutterUnit x_out, y_out;
  gchar *command;
  gint button;
  
  if (!clutter_actor_transform_stage_point (actor,
                                            CLUTTER_UNITS_FROM_INT (event->x),
                                            CLUTTER_UNITS_FROM_INT (event->y),
                                            &x_out, &y_out))
    return FALSE;
  
  switch (event->direction)
    {
    case CLUTTER_SCROLL_UP :
      button = 4;
      break;
    default:
    case CLUTTER_SCROLL_DOWN :
      button = 5;
      break;
    case CLUTTER_SCROLL_LEFT :
      button = 6;
      break;
    case CLUTTER_SCROLL_RIGHT :
      button = 7;
      break;
    }
  
  priv = CLUTTER_MOZEMBED (actor)->priv;
  command = g_strdup_printf ("button-press %d %d %d %d",
                             CLUTTER_UNITS_TO_INT (x_out),
                             CLUTTER_UNITS_TO_INT (y_out),
                             button,
                             1);
  send_command (CLUTTER_MOZEMBED (actor), command);
  g_free (command);
  
  return TRUE;
}

static void
clutter_mozembed_class_init (ClutterMozEmbedClass *klass)
{
  GObjectClass      *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);

  g_type_class_add_private (klass, sizeof (ClutterMozEmbedPrivate));

  object_class->get_property = clutter_mozembed_get_property;
  object_class->set_property = clutter_mozembed_set_property;
  object_class->dispose = clutter_mozembed_dispose;
  object_class->finalize = clutter_mozembed_finalize;
  
  actor_class->allocate             = clutter_mozembed_allocate;
  actor_class->paint                = clutter_mozembed_paint;
  actor_class->pick                 = clutter_mozembed_pick;
  actor_class->motion_event         = clutter_mozembed_motion_event;
  actor_class->button_press_event   = clutter_mozembed_button_press_event;
  actor_class->button_release_event = clutter_mozembed_button_release_event;
  actor_class->key_press_event      = clutter_mozembed_key_press_event;
  actor_class->key_release_event    = clutter_mozembed_key_release_event;
  actor_class->scroll_event         = clutter_mozembed_scroll_event;

  g_object_class_install_property (object_class,
                                   PROP_LOCATION,
                                   g_param_spec_string ("location",
                                                        "Location",
                                                        "Current URL.",
                                                        "about:blank",
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_TITLE,
                                   g_param_spec_string ("title",
                                                        "Title",
                                                        "Current page title.",
                                                        "",
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  signals[PROGRESS] =
    g_signal_new ("progress",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterMozEmbedClass, progress),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__DOUBLE,
                  G_TYPE_NONE, 1, G_TYPE_DOUBLE);

  signals[NET_START] =
    g_signal_new ("net-start",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterMozEmbedClass, net_start),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[NET_STOP] =
    g_signal_new ("net-stop",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterMozEmbedClass, net_stop),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}

static void
clutter_mozembed_init (ClutterMozEmbed *self)
{
  static gint spawned_windows = 0;
  gint standard_input;
  gboolean success;

  gchar *argv[] = {
    "mozheadless",
    NULL, /* Pipe file */
    NULL, /* SHM name */
    NULL
  };

  GError *error = NULL;

  ClutterMozEmbedPrivate *priv = self->priv = MOZEMBED_PRIVATE (self);
  
  priv->motion_ack = TRUE;

  clutter_actor_set_reactive (CLUTTER_ACTOR (self), TRUE);
  
  /* Set up out-of-process renderer */
  
  /* Generate names for pipe/shm */
  argv[1] = g_strdup_printf ("./%d-%d", getpid (), spawned_windows);
  argv[2] = priv->shm_name = g_strdup_printf ("/mozheadless-%d",
                                              spawned_windows++);
  
  /* Create named pipe */
  mkfifo (argv[1], S_IRUSR | S_IWUSR);
  
  /* Spawn renderer */
  success = g_spawn_async_with_pipes (NULL,
                                      argv,
                                      NULL,
                                      G_SPAWN_SEARCH_PATH,
                                      NULL,
                                      NULL,
                                      NULL,
                                      &standard_input,
                                      NULL,
                                      NULL,
                                      &error);
  
  if (!success)
    {
      g_warning ("Error spawning renderer: %s", error->message);
      g_error_free (error);
      return;
    }

  /* Read from named pipe */
  /*FILE *input = fopen (argv[1], "r");
  priv->input = g_io_channel_unix_new (fileno (input));*/
  priv->input = g_io_channel_new_file (argv[1], "r", NULL);
  g_io_channel_set_encoding (priv->input, NULL, NULL);
  g_io_channel_set_buffered (priv->input, FALSE);
  g_io_channel_set_close_on_unref (priv->input, TRUE);
  g_io_add_watch (priv->input,
                  G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_NVAL | G_IO_HUP,
                  (GIOFunc)input_io_func,
                  self);

  g_free (argv[1]);
  
  /* Open up standard input */
  priv->output = fdopen (standard_input, "w");
}

ClutterActor *
clutter_mozembed_new (void)
{
  return CLUTTER_ACTOR (g_object_new (CLUTTER_TYPE_MOZEMBED, NULL));
}

void
clutter_mozembed_open (ClutterMozEmbed *mozembed, const gchar *uri)
{
  gchar *command = g_strdup_printf ("open %s", uri);
  send_command (mozembed, command);
  g_free (command);
}

const gchar *
clutter_mozembed_get_location (ClutterMozEmbed *mozembed)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;
  return priv->location;
}

const gchar *
clutter_mozembed_get_title (ClutterMozEmbed *mozembed)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;
  return priv->title;
}

gboolean
clutter_mozembed_can_go_back (ClutterMozEmbed *mozembed)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;

  send_command (mozembed, "can-go-back?");
  block_until_feedback (mozembed, "back");
  
  return priv->can_go_back;
}

gboolean
clutter_mozembed_can_go_forward (ClutterMozEmbed *mozembed)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;

  send_command (mozembed, "can-go-forward?");
  block_until_feedback (mozembed, "forward");
  
  return priv->can_go_forward;
}

void
clutter_mozembed_back (ClutterMozEmbed *mozembed)
{
  send_command (mozembed, "back");
}

void
clutter_mozembed_forward (ClutterMozEmbed *mozembed)
{
  send_command (mozembed, "forward");
}

