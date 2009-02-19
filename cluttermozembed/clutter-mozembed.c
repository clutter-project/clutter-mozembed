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

G_DEFINE_TYPE (ClutterMozEmbed, clutter_mozembed, CLUTTER_TYPE_TEXTURE)

#define MOZEMBED_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), CLUTTER_TYPE_MOZEMBED, ClutterMozEmbedPrivate))

enum
{
  PROP_0,
 
  PROP_LOCATION,
  PROP_TITLE,
  PROP_READONLY,
  PROP_PIPE,
  PROP_SHM,
};

enum
{
  PROGRESS,
  NET_START,
  NET_STOP,
  CRASHED,
  
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

struct _ClutterMozEmbedPrivate
{
  GIOChannel      *input;
  FILE            *output;
  guint            watch_id;
  GPid             child_pid;
  
  gchar           *pipe_file;
  gchar           *shm_name;
  gboolean         opened_shm;
  gboolean         new_data;
  int              shm_fd;
  
  void            *image_data;
  int              image_size;
  
  gboolean         read_only;

  gboolean            motion_ack;
  gulong              motion_throttle;
  gint                motion_x;
  gint                motion_y;
  ClutterModifierType motion_m;
  
  /* Variables for synchronous calls */
  const gchar     *sync_call;
  gboolean         can_go_back;
  gboolean         can_go_forward;
  
  /* Locally cached properties */
  gchar           *location;
  gchar           *title;
  
  /* Scroll coordinates for sliding-window (FUTURE) */
  /*
  gint             scroll_x;
  gint             scroll_y;
  gint             page_width;
  gint             page_height;*/
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
update (ClutterMozEmbed *self,
        gint             x,
        gint             y,
        gint             width,
        gint             height,
        gint             surface_width,
        gint             surface_height)
{
  guint tex_width, tex_height;

  ClutterMozEmbedPrivate *priv = self->priv;
  GError *error = NULL;
  
  if (!priv->opened_shm)
    {
      /*g_debug ("Opening shm");*/
      priv->opened_shm = TRUE;
      priv->shm_fd = shm_open (priv->shm_name, O_RDONLY, 0444);
      if (priv->shm_fd == -1)
        g_error ("Error opening shared memory");
    }
  
  clutter_actor_get_size (CLUTTER_ACTOR (self), &tex_width, &tex_height);
  
  /* If the surface size of the mozilla window is different to our texture 
   * size, ignore it - it just means we've resized in the middle of the
   * backend drawing and we'll get a new update almost immediately anyway.
   */
  if ((surface_width != tex_width) || (surface_height != tex_height))
    return;
  
  if (!priv->image_data)
    {
      /*g_debug ("mmapping data");*/
      priv->image_size = surface_width * surface_height * 4;
      priv->image_data = mmap (NULL, priv->image_size, PROT_READ,
                               MAP_SHARED, priv->shm_fd, 0);
    }
  
  /*g_debug ("Reading data");*/

  /* Copy data to texture */
  if (!clutter_texture_set_area_from_rgb_data (
         CLUTTER_TEXTURE (self),
         priv->image_data + (x*4) + (y*surface_width*4),
         TRUE,
         x,
         y,
         width,
         height,
         surface_width*4,
         4,
         CLUTTER_TEXTURE_RGB_FLAG_BGR,
         &error))
    {
      g_warning ("Error setting texture data: %s", error->message);
      g_error_free (error);
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
      gint x, y, width, height, surface_width, surface_height;
      
      gchar *params[6];
      if (!separate_strings (params, G_N_ELEMENTS (params), detail))
        return;
      
      x = atoi (params[0]);
      y = atoi (params[1]);
      width = atoi (params[2]);
      height = atoi (params[3]);
      surface_width = atoi (params[4]);
      surface_height = atoi (params[5]);
      
      priv->new_data = TRUE;
      
      update (self, x, y, width, height, surface_width, surface_height);
      
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
  ClutterMozEmbedPrivate *priv = self->priv;

  /* FYI: Maximum URL length in IE is 2083 characters */
  gchar buf[4096];
  gsize length;

  GError *error = NULL;
  GIOStatus status;
  
  switch (condition) {
    case G_IO_PRI :
    case G_IO_IN :
      
      status = g_io_channel_read_chars (source, buf, sizeof (buf), &length,
                                        &error);
      if (status == G_IO_STATUS_NORMAL) {
        gsize current_length = 0;
        while (current_length < length)
          {
            gchar *feedback = &buf[current_length];
            current_length += strlen (&buf[current_length]) + 1;
            process_feedback (self, feedback);
          }
        return TRUE;
      } else if (status == G_IO_STATUS_ERROR && error) {
        g_warning ("Error reading from source: %s", error->message);
        g_error_free (error);
      } else if (status == G_IO_STATUS_EOF) {
        g_warning ("Reached end of input pipe");
      }
      break;

    case G_IO_ERR :
    case G_IO_NVAL :
      g_warning ("Error or invalid request");
      break;
    
    case G_IO_HUP :
      g_warning ("Hung up");

      /* prevent any more calls to this function */
      if (priv->watch_id) {
        g_source_remove (priv->watch_id);
        priv->watch_id = 0;
      }

      break;
    
    default :
      g_warning ("Unhandled IO condition");
      return FALSE;
  }

  g_signal_emit (self, signals[CRASHED], 0);
  
  return TRUE;
}

static void
send_command (ClutterMozEmbed *mozembed, const gchar *command)
{
  if (!command)
    return;

  if (!mozembed->priv->output)
    {
      g_warning ("Child process is not available");
      return;
    }
  
  if ((mozembed->priv->read_only) && (command[strlen(command)-1] != '?'))
    return;
  
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

  case PROP_READONLY :
    g_value_set_boolean (value, self->priv->read_only);
    break;

  case PROP_PIPE :
    g_value_set_string (value, self->priv->pipe_file);
    break;

  case PROP_SHM :
    g_value_set_string (value, self->priv->shm_name);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
clutter_mozembed_set_property (GObject *object, guint property_id,
                              const GValue *value, GParamSpec *pspec)
{
  ClutterMozEmbedPrivate *priv = CLUTTER_MOZEMBED (object)->priv;

  switch (property_id) {
  case PROP_READONLY :
    priv->read_only = g_value_get_boolean (value);
    break;

  case PROP_PIPE :
    priv->pipe_file = g_value_dup_string (value);
    break;

  case PROP_SHM :
    priv->shm_name = g_value_dup_string (value);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
clutter_mozembed_dispose (GObject *object)
{
  ClutterMozEmbedPrivate *priv = CLUTTER_MOZEMBED (object)->priv;
  
  if (priv->child_pid)
    {
      g_spawn_close_pid (priv->child_pid);
      priv->child_pid = 0;
    }

  if (priv->input)
    {
      GError *error = NULL;

      if (priv->watch_id) {
        g_source_remove (priv->watch_id);
        priv->watch_id = 0;
      }
      
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
  
  g_free (priv->location);
  g_free (priv->title);
  g_free (priv->pipe_file);
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
  gint width, height, tex_width, tex_height;
  ClutterMozEmbedPrivate *priv = CLUTTER_MOZEMBED (actor)->priv;

  width = CLUTTER_UNITS_TO_INT (box->x2 - box->x1);
  height = CLUTTER_UNITS_TO_INT (box->y2 - box->y1);
  
  clutter_texture_get_base_size (CLUTTER_TEXTURE (actor),
                                 &tex_width, &tex_height);
  
  if ((tex_width != width) || (tex_height != height))
    {
      /* Fill the texture with white when resizing */
      guchar *data = g_malloc (width * height * 4);
      memset (data, 0xff, width * height * 4);
      clutter_texture_set_from_rgb_data (CLUTTER_TEXTURE (actor),
                                         data, TRUE, width, height,
                                         width * 4, 4, 0, NULL);
      g_free (data);

      /* Unmap previous texture data */
      if (priv->image_data)
        {
          munmap (priv->image_data, priv->image_size);
          priv->image_data = NULL;
        }
      
      /* Send a resize command to the back-end */
      command = g_strdup_printf ("resize %d %d", width, height);
      send_command (CLUTTER_MOZEMBED (actor), command);
      g_free (command);
    }
  
  CLUTTER_ACTOR_CLASS (clutter_mozembed_parent_class)->
    allocate (actor, box, absolute_origin_changed);
}

static void
clutter_mozembed_paint (ClutterActor *actor)
{
  ClutterMozEmbed *self = CLUTTER_MOZEMBED (actor);
  ClutterMozEmbedPrivate *priv = self->priv;

  CLUTTER_ACTOR_CLASS (clutter_mozembed_parent_class)->paint (actor);
  
  if (priv->new_data && CLUTTER_ACTOR_IS_VISIBLE (actor))
    {
      priv->new_data = FALSE;
      send_command (self, "ack");
    }
}

static void
clutter_mozembed_pick (ClutterActor *actor, const ClutterColor *c)
{
  guint width, height;
  
  cogl_set_source_color4ub (c->red, c->green, c->blue, c->alpha);
  clutter_actor_get_size (actor, &width, &height);
  cogl_rectangle (0, 0, width, height);
}

static MozHeadlessModifier
clutter_mozembed_get_modifier (ClutterModifierType modifiers)
{
  MozHeadlessModifier mozifiers = 0;
  mozifiers |= (modifiers & CLUTTER_SHIFT_MASK) ? MOZ_KEY_SHIFT_MASK : 0;
  mozifiers |= (modifiers & CLUTTER_CONTROL_MASK) ? MOZ_KEY_CONTROL_MASK : 0;
  mozifiers |= (modifiers & CLUTTER_MOD1_MASK) ? MOZ_KEY_ALT_MASK : 0;
  mozifiers |= (modifiers & CLUTTER_META_MASK) ? MOZ_KEY_META_MASK : 0;
  
  return mozifiers;
}

static gboolean
motion_idle (ClutterMozEmbed *self)
{
  gchar *command;
  ClutterMozEmbedPrivate *priv = self->priv;

  if (!priv->motion_ack)
    return TRUE;

  command = g_strdup_printf ("motion %d %d %u", priv->motion_x, priv->motion_y,
                             clutter_mozembed_get_modifier (priv->motion_m));
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
  
  priv = CLUTTER_MOZEMBED (actor)->priv;

  if (!clutter_actor_transform_stage_point (actor,
                                            CLUTTER_UNITS_FROM_INT (event->x),
                                            CLUTTER_UNITS_FROM_INT (event->y),
                                            &x_out, &y_out))
    return FALSE;
  
  priv->motion_x = CLUTTER_UNITS_TO_INT (x_out);
  priv->motion_y = CLUTTER_UNITS_TO_INT (y_out);
  priv->motion_m = event->modifier_state;
  
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
  
  priv = CLUTTER_MOZEMBED (actor)->priv;

  if (!clutter_actor_transform_stage_point (actor,
                                            CLUTTER_UNITS_FROM_INT (event->x),
                                            CLUTTER_UNITS_FROM_INT (event->y),
                                            &x_out, &y_out))
    return FALSE;
  
  clutter_grab_pointer (actor);
  
  command =
    g_strdup_printf ("button-press %d %d %d %d %u",
                     CLUTTER_UNITS_TO_INT (x_out),
                     CLUTTER_UNITS_TO_INT (y_out),
                     event->button,
                     event->click_count,
                     clutter_mozembed_get_modifier (event->modifier_state));
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
  
  priv = CLUTTER_MOZEMBED (actor)->priv;

  if (!clutter_actor_transform_stage_point (actor,
                                            CLUTTER_UNITS_FROM_INT (event->x),
                                            CLUTTER_UNITS_FROM_INT (event->y),
                                            &x_out, &y_out))
    return FALSE;
  
  command =
    g_strdup_printf ("button-release %d %d %d %u",
                     CLUTTER_UNITS_TO_INT (x_out),
                     CLUTTER_UNITS_TO_INT (y_out),
                     event->button,
                     clutter_mozembed_get_modifier (event->modifier_state));
  send_command (CLUTTER_MOZEMBED (actor), command);
  g_free (command);
  
  return TRUE;
}

static gboolean
clutter_mozembed_get_keyval (ClutterKeyEvent *event, guint *keyval)
{
  switch (event->keyval)
    {
    case CLUTTER_Cancel :
      *keyval = MOZ_KEY_CANCEL;
      return TRUE;
    case CLUTTER_Help :
      *keyval = MOZ_KEY_HELP;
      return TRUE;
    case CLUTTER_BackSpace :
      *keyval = MOZ_KEY_BACK_SPACE;
      return TRUE;
    case CLUTTER_Tab :
      *keyval = MOZ_KEY_TAB;
      return TRUE;
    case CLUTTER_Return :
      *keyval = MOZ_KEY_RETURN;
      return TRUE;
    case CLUTTER_KP_Enter :
      *keyval = MOZ_KEY_ENTER;
      return TRUE;
    case CLUTTER_Shift_L :
    case CLUTTER_Shift_R :
      *keyval = MOZ_KEY_SHIFT;
      return TRUE;
    case CLUTTER_Control_L :
    case CLUTTER_Control_R :
      *keyval = MOZ_KEY_CONTROL;
      return TRUE;
    case CLUTTER_Alt_L :
    case CLUTTER_Alt_R :
      *keyval = MOZ_KEY_ALT;
      return TRUE;
    case CLUTTER_Pause :
      *keyval = MOZ_KEY_PAUSE;
      return TRUE;
    case CLUTTER_Caps_Lock :
      *keyval = MOZ_KEY_CAPS_LOCK;
      return TRUE;
    case CLUTTER_Escape :
      *keyval = MOZ_KEY_ESCAPE;
      return TRUE;
    case CLUTTER_space :
      *keyval = MOZ_KEY_SPACE;
      return TRUE;
    case CLUTTER_Page_Up :
      *keyval = MOZ_KEY_PAGE_UP;
      return TRUE;
    case CLUTTER_Page_Down :
      *keyval = MOZ_KEY_PAGE_DOWN;
      return TRUE;
    case CLUTTER_End :
      *keyval = MOZ_KEY_END;
      return TRUE;
    case CLUTTER_Home :
      *keyval = MOZ_KEY_HOME;
      return TRUE;
    case CLUTTER_Left:
      *keyval = MOZ_KEY_LEFT;
      return TRUE;
    case CLUTTER_Up:
      *keyval = MOZ_KEY_UP;
      return TRUE;
    case CLUTTER_Right:
      *keyval = MOZ_KEY_RIGHT;
      return TRUE;
    case CLUTTER_Down:
      *keyval = MOZ_KEY_DOWN;
      return TRUE;
    case CLUTTER_Print:
      *keyval = MOZ_KEY_PRINTSCREEN;
      return TRUE;
    case CLUTTER_Insert:
      *keyval = MOZ_KEY_INSERT;
      return TRUE;
    case CLUTTER_Delete:
      *keyval = MOZ_KEY_DELETE;
      return TRUE;
    case CLUTTER_semicolon:
      *keyval = MOZ_KEY_SEMICOLON;
      return TRUE;
    case CLUTTER_equal:
      *keyval = MOZ_KEY_EQUALS;
      return TRUE;
    case CLUTTER_Menu:
      *keyval = MOZ_KEY_CONTEXT_MENU;
      return TRUE;
    case CLUTTER_KP_0:
      *keyval = MOZ_KEY_NUMPAD0;
      return TRUE;
    case CLUTTER_KP_1:
      *keyval = MOZ_KEY_NUMPAD1;
      return TRUE;
    case CLUTTER_KP_2:
      *keyval = MOZ_KEY_NUMPAD2;
      return TRUE;
    case CLUTTER_KP_3:
      *keyval = MOZ_KEY_NUMPAD3;
      return TRUE;
    case CLUTTER_KP_4:
      *keyval = MOZ_KEY_NUMPAD4;
      return TRUE;
    case CLUTTER_KP_5:
      *keyval = MOZ_KEY_NUMPAD5;
      return TRUE;
    case CLUTTER_KP_6:
      *keyval = MOZ_KEY_NUMPAD6;
      return TRUE;
    case CLUTTER_KP_7:
      *keyval = MOZ_KEY_NUMPAD7;
      return TRUE;
    case CLUTTER_KP_8:
      *keyval = MOZ_KEY_NUMPAD8;
      return TRUE;
    case CLUTTER_KP_9:
      *keyval = MOZ_KEY_NUMPAD9;
      return TRUE;
    case CLUTTER_KP_Add:
      *keyval = MOZ_KEY_NUMPAD0;
      return TRUE;
    case CLUTTER_KP_Separator:
      *keyval = MOZ_KEY_SEPARATOR;
      return TRUE;
    case CLUTTER_KP_Subtract:
      *keyval = MOZ_KEY_SUBTRACT;
      return TRUE;
    case CLUTTER_KP_Decimal:
      *keyval = MOZ_KEY_DECIMAL;
      return TRUE;
    case CLUTTER_KP_Divide:
      *keyval = MOZ_KEY_DIVIDE;
      return TRUE;
    case CLUTTER_F1:
      *keyval = MOZ_KEY_F1;
      return TRUE;
    case CLUTTER_F2:
      *keyval = MOZ_KEY_F2;
      return TRUE;
    case CLUTTER_F3:
      *keyval = MOZ_KEY_F3;
      return TRUE;
    case CLUTTER_F4:
      *keyval = MOZ_KEY_F4;
      return TRUE;
    case CLUTTER_F5:
      *keyval = MOZ_KEY_F5;
      return TRUE;
    case CLUTTER_F6:
      *keyval = MOZ_KEY_F6;
      return TRUE;
    case CLUTTER_F7:
      *keyval = MOZ_KEY_F7;
      return TRUE;
    case CLUTTER_F8:
      *keyval = MOZ_KEY_F8;
      return TRUE;
    case CLUTTER_F9:
      *keyval = MOZ_KEY_F9;
      return TRUE;
    case CLUTTER_F10:
      *keyval = MOZ_KEY_F10;
      return TRUE;
    case CLUTTER_F11:
      *keyval = MOZ_KEY_F11;
      return TRUE;
    case CLUTTER_F12:
      *keyval = MOZ_KEY_F12;
      return TRUE;
    case CLUTTER_F13:
      *keyval = MOZ_KEY_F13;
      return TRUE;
    case CLUTTER_F14:
      *keyval = MOZ_KEY_F14;
      return TRUE;
    case CLUTTER_F15:
      *keyval = MOZ_KEY_F15;
      return TRUE;
    case CLUTTER_F16:
      *keyval = MOZ_KEY_F16;
      return TRUE;
    case CLUTTER_F17:
      *keyval = MOZ_KEY_F17;
      return TRUE;
    case CLUTTER_F18:
      *keyval = MOZ_KEY_F18;
      return TRUE;
    case CLUTTER_F19:
      *keyval = MOZ_KEY_F19;
      return TRUE;
    case CLUTTER_F20:
      *keyval = MOZ_KEY_F20;
      return TRUE;
    case CLUTTER_F21:
      *keyval = MOZ_KEY_F21;
      return TRUE;
    case CLUTTER_F22:
      *keyval = MOZ_KEY_F22;
      return TRUE;
    case CLUTTER_F23:
      *keyval = MOZ_KEY_F23;
      return TRUE;
    case CLUTTER_F24:
      *keyval = MOZ_KEY_F24;
      return TRUE;
    case CLUTTER_Num_Lock:
      *keyval = MOZ_KEY_NUM_LOCK;
      return TRUE;
    case CLUTTER_Scroll_Lock:
      *keyval = MOZ_KEY_SCROLL_LOCK;
      return TRUE;
    }
  

  if (g_ascii_isalnum (event->unicode_value))
    {
      *keyval = event->unicode_value;
      return TRUE;
    }

  *keyval = 0;

  return FALSE;
}

static gboolean
clutter_mozembed_key_press_event (ClutterActor *actor, ClutterKeyEvent *event)
{
  ClutterMozEmbedPrivate *priv;
  gchar *command;
  guint keyval;

  priv = CLUTTER_MOZEMBED (actor)->priv;

  if ((!clutter_mozembed_get_keyval (event, &keyval)) &&
      (event->unicode_value == '\0'))
    return FALSE;

  command = g_strdup_printf ("key-press %u %u %u", keyval, event->unicode_value,
                             clutter_mozembed_get_modifier (event->modifier_state));
  send_command (CLUTTER_MOZEMBED (actor), command);
  g_free (command);

  return TRUE;
}

static gboolean
clutter_mozembed_key_release_event (ClutterActor *actor, ClutterKeyEvent *event)
{
  ClutterMozEmbedPrivate *priv;
  guint keyval;
  
  priv = CLUTTER_MOZEMBED (actor)->priv;

  if (clutter_mozembed_get_keyval (event, &keyval))
    {
      gchar *command =
        g_strdup_printf ("key-release %u %u", keyval,
                         clutter_mozembed_get_modifier (event->modifier_state));
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
  
  priv = CLUTTER_MOZEMBED (actor)->priv;

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
  
  command =
    g_strdup_printf ("button-press %d %d %d %d %u",
                     CLUTTER_UNITS_TO_INT (x_out),
                     CLUTTER_UNITS_TO_INT (y_out),
                     button,
                     1,
                     clutter_mozembed_get_modifier (event->modifier_state));
  send_command (CLUTTER_MOZEMBED (actor), command);
  g_free (command);
  
  return TRUE;
}

static void
clutter_mozembed_constructed (GObject *object)
{
  static gint spawned_windows = 0;
  gint standard_input;
  gboolean success;

  gchar *argv[] = {
    "mozheadless",  /* TODO: We should probably use an absolute path here.. */
    NULL, /* Pipe file */
    NULL, /* SHM name */
    NULL
  };

  ClutterMozEmbed *self = CLUTTER_MOZEMBED (object);
  ClutterMozEmbedPrivate *priv = self->priv;
  GError *error = NULL;

  /* Set up out-of-process renderer */
  
  /* Generate names for pipe/shm, if not provided */
  if (priv->pipe_file)
    {
      /* Don't overwrite user-supplied pipe files */
      if (g_file_test (priv->pipe_file, G_FILE_TEST_EXISTS))
        {
          g_free (priv->pipe_file);
          priv->pipe_file = NULL;
        }
    }
  
  if (!priv->pipe_file)
    priv->pipe_file = g_strdup_printf ("%s/clutter-mozembed-%d-%d",
                                       g_get_tmp_dir (), getpid (),
                                       spawned_windows);
  argv[1] = priv->pipe_file;
  
  if (!priv->shm_name)
    priv->shm_name = g_strdup_printf ("/mozheadless-%d-%d",
                                      getpid (), spawned_windows);
  argv[2] = priv->shm_name;
  
  spawned_windows++;
  
  /* Create named pipe */
  if (mkfifo (argv[1], S_IRUSR | S_IWUSR) == -1)
    {
      g_warning ("Error opening pipe");
      return;
    }
  
  /* Spawn renderer */
  success = g_spawn_async_with_pipes (NULL,
                                      argv,
                                      NULL,
                                      G_SPAWN_SEARCH_PATH,
                                      NULL,
                                      NULL,
                                      &priv->child_pid,
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
  priv->input = g_io_channel_new_file (argv[1], "r", NULL);
  g_io_channel_set_encoding (priv->input, NULL, NULL);
  g_io_channel_set_buffered (priv->input, FALSE);
  g_io_channel_set_close_on_unref (priv->input, TRUE);
  priv->watch_id = g_io_add_watch (priv->input,
                                   G_IO_IN | G_IO_PRI | G_IO_ERR |
                                   G_IO_NVAL | G_IO_HUP,
                                   (GIOFunc)input_io_func,
                                   self);

  /* Open up standard input */
  priv->output = fdopen (standard_input, "w");
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
  object_class->constructed = clutter_mozembed_constructed;
  
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

  g_object_class_install_property (object_class,
                                   PROP_READONLY,
                                   g_param_spec_boolean ("read-only",
                                                         "Read-only",
                                                         "Whether to disallow "
                                                         "input.",
                                                         FALSE,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_NICK |
                                                         G_PARAM_STATIC_BLURB |
                                                         G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (object_class,
                                   PROP_PIPE,
                                   g_param_spec_string ("pipe",
                                                        "Pipe file",
                                                        "Communications pipe "
                                                        "file name.",
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB |
                                                        G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (object_class,
                                   PROP_SHM,
                                   g_param_spec_string ("shm",
                                                        "Named SHM",
                                                        "Named shared memory "
                                                        "region for image "
                                                        "buffer.",
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB |
                                                        G_PARAM_CONSTRUCT_ONLY));

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

  signals[CRASHED] =
    g_signal_new ("crashed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterMozEmbedClass, crashed),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}

static void
clutter_mozembed_init (ClutterMozEmbed *self)
{
  ClutterMozEmbedPrivate *priv = self->priv = MOZEMBED_PRIVATE (self);
  
  priv->motion_ack = TRUE;
  clutter_actor_set_reactive (CLUTTER_ACTOR (self), TRUE);
  
  /* Turn off sync-size (we manually size the texture on allocate) */
  g_object_set (G_OBJECT (self), "sync-size", FALSE, NULL);
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

void
clutter_mozembed_stop (ClutterMozEmbed *mozembed)
{
  send_command (mozembed, "stop");
}

void
clutter_mozembed_refresh (ClutterMozEmbed *mozembed)
{
  send_command (mozembed, "refresh");
}

void
clutter_mozembed_reload (ClutterMozEmbed *mozembed)
{
  send_command (mozembed, "reload");
}

