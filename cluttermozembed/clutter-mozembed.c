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
#include <config.h>

#include "clutter-mozembed.h"
#include "clutter-mozembed-private.h"
#include "clutter-mozembed-marshal.h"
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <moz-headless.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#ifdef SUPPORT_PLUGINS
#include <X11/extensions/Xcomposite.h>
#include <clutter/x11/clutter-x11.h>
#include <clutter/glx/clutter-glx.h>
#include <gdk/gdkx.h>
#endif

G_DEFINE_TYPE (ClutterMozEmbed, clutter_mozembed, CLUTTER_TYPE_TEXTURE)

#define MOZEMBED_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), CLUTTER_TYPE_MOZEMBED, ClutterMozEmbedPrivate))

/* #define DEBUG_PLUGIN_VIEWPORT */

enum
{
  PROP_0,
 
  PROP_LOCATION,
  PROP_TITLE,
  PROP_ICON,
  PROP_READONLY,
  PROP_INPUT,
  PROP_OUTPUT,
  PROP_SHM,
  PROP_SPAWN,
  PROP_SMOOTH_SCROLL,
  PROP_DOC_WIDTH,
  PROP_DOC_HEIGHT,
  PROP_SCROLL_X,
  PROP_SCROLL_Y,
  PROP_POLL_TIMEOUT,
  PROP_CONNECT_TIMEOUT,
  PROP_CAN_GO_BACK,
  PROP_CAN_GO_FORWARD,
  PROP_CURSOR
};

enum
{
  PROGRESS,
  NET_START,
  NET_STOP,
  CRASHED,
  NEW_WINDOW,
  CLOSED,
  LINK_MESSAGE,
  SIZE_REQUEST,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

struct _ClutterMozEmbedPrivate
{
  GFileMonitor    *monitor;
  GIOChannel      *input;
  GIOChannel      *output;
  guint            watch_id;
  GPid             child_pid;
  
  gchar           *input_file;
  gchar           *output_file;
  gchar           *shm_name;
  gboolean         opened_shm;
  gboolean         new_data;
  int              shm_fd;
  gboolean         spawn;
  
  void            *image_data;
  int              image_size;
  
  gboolean         read_only;

  gboolean            motion_ack;
  gboolean            pending_motion;
  gint                motion_x;
  gint                motion_y;
  ClutterModifierType motion_m;

  /* Variables for synchronous calls */
  const gchar     *sync_call;

  /* Locally cached properties */
  gchar           *location;
  gchar           *title;
  gchar           *icon;
  gint             doc_width;
  gint             doc_height;
  gint             scroll_x;
  gint             scroll_y;
  gboolean         is_loading;
  gdouble          progress;
  gboolean         can_go_back;
  gboolean         can_go_forward;

  /* Offsets for async (smooth) scrolling mode */
  gint             offset_x;
  gint             offset_y;
  gboolean         async_scroll;

  /* Connection timeout variables */
  guint            poll_source;
  guint            poll_timeout;
  guint            poll_timeout_source;
  guint            connect_timeout;
  guint            connect_timeout_source;

#ifdef SUPPORT_PLUGINS
  /* The window given for us to parent a plugin viewport onto */
  GdkWindow       *toplevel_window;

  /* A window created for the purpose of clipping input for
   * plugin windows. */
  Window           plugin_viewport;

  GList           *plugin_windows;

  Window           stage_xwin;

  gboolean         pending_open;
  gchar           *pending_url;
#endif

  MozHeadlessCursorType cursor;
};

static void clutter_mozembed_open_pipes (ClutterMozEmbed *self);
static MozHeadlessModifier
  clutter_mozembed_get_modifier (ClutterModifierType modifiers);

#ifdef SUPPORT_PLUGINS
typedef struct _PluginWindow
{
  gint          x, y;
  ClutterActor *plugin_tfp;
} PluginWindow;

static void
clutter_mozembed_allocate_plugins (ClutterMozEmbed *mozembed,
                                   gboolean         absolute_origin_changed);

static gboolean
clutter_mozembed_init_viewport (ClutterMozEmbed *mozembed);

static int trapped_x_error = 0;
static int (*prev_error_handler) (Display *, XErrorEvent *);
#endif

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
clamp_offset (ClutterMozEmbed *self)
{
  gint width, height;

  ClutterMozEmbedPrivate *priv = self->priv;

  clutter_texture_get_base_size (CLUTTER_TEXTURE (self), &width, &height);
  priv->offset_x = CLAMP (priv->offset_x,
                          -(priv->doc_width - width - priv->scroll_x),
                          priv->scroll_x);
  priv->offset_y = CLAMP (priv->offset_y,
                          -(priv->doc_height - height - priv->scroll_y),
                          priv->scroll_y);
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
  gboolean result;
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
  if (!priv->read_only)
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
  if ((x == 0) && (y == 0) && (width == surface_width) &&
      (height == surface_height))
    result = clutter_texture_set_from_rgb_data (CLUTTER_TEXTURE (self),
                                                priv->image_data,
                                                TRUE,
                                                surface_width,
                                                surface_height,
                                                surface_width * 4,
                                                4,
                                                CLUTTER_TEXTURE_RGB_FLAG_BGR,
                                                &error);
  else
    result = clutter_texture_set_area_from_rgb_data (CLUTTER_TEXTURE (self),
                                                     priv->image_data + (x*4) +
                                                     (y*surface_width*4),
                                                     TRUE,
                                                     x,
                                                     y,
                                                     width,
                                                     height,
                                                     surface_width * 4,
                                                     4,
                                                     CLUTTER_TEXTURE_RGB_FLAG_BGR,
                                                     &error);

  if (!result)
    {
      g_warning ("Error setting texture data: %s", error->message);
      g_error_free (error);
    }
}

void
send_command (ClutterMozEmbed *mozembed, const gchar *command)
{
  if (!command)
    return;

  if (!mozembed->priv->output)
    {
      g_warning ("Child process is not available");
      return;
    }
  
  if ((mozembed->priv->read_only) && (command[strlen(command)-1] != '?') &&
      (command[strlen(command)-1] != '!'))
    return;
  
  /*g_debug ("Sending command: %s", command);*/
  
  /* TODO: Error handling */
  g_io_channel_write_chars (mozembed->priv->output, command,
                            strlen (command) + 1, NULL, NULL);
  g_io_channel_flush (mozembed->priv->output, NULL);
}

static void
send_motion_event (ClutterMozEmbed *self)
{
  ClutterMozEmbedPrivate *priv = self->priv;
  gchar *command = g_strdup_printf ("motion %d %d %u",
                                    priv->motion_x, priv->motion_y,
                                    clutter_mozembed_get_modifier (
                                      priv->motion_m));
  send_command (self, command);
  g_free (command);
  priv->pending_motion = FALSE;
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
      gint x, y, width, height, surface_width, surface_height, dx, dy, sx, sy;
      
      gchar *params[10];
      if (!separate_strings (params, G_N_ELEMENTS (params), detail))
        return;

      x = atoi (params[0]);
      y = atoi (params[1]);
      width = atoi (params[2]);
      height = atoi (params[3]);
      surface_width = atoi (params[4]);
      surface_height = atoi (params[5]);
      sx = atoi (params[6]);
      sy = atoi (params[7]);
      dx = atoi (params[8]);
      dy = atoi (params[9]);
      
      priv->new_data = TRUE;

      if (priv->doc_width != dx)
        {
          priv->doc_width = dx;
          g_object_notify (G_OBJECT (self), "doc-width");
        }
      if (priv->doc_height != dy)
        {
          priv->doc_height = dy;
          g_object_notify (G_OBJECT (self), "doc-height");
        }

      /* Update async scrolling offset */
      priv->offset_x += sx - priv->scroll_x;
      priv->offset_y += sy - priv->scroll_y;

      /* Clamp in case document size has changed */
      if (priv->scroll_x != sx)
        {
          priv->scroll_x = sx;
          g_object_notify (G_OBJECT (self), "scroll-x");
        }
      if (priv->scroll_y != sy)
        {
          priv->scroll_y = sy;
          g_object_notify (G_OBJECT (self), "scroll-y");
        }
      clamp_offset (self);
      
      update (self, x, y, width, height, surface_width, surface_height);
      
      clutter_actor_queue_redraw (CLUTTER_ACTOR (self));
    }
  else if (g_str_equal (command, "mack"))
    {
      priv->motion_ack = TRUE;

      if (priv->pending_motion)
        {
          send_motion_event (self);
          priv->motion_ack = FALSE;
          priv->pending_motion = FALSE;
        }
    }
  else if (g_str_equal (command, "progress"))
    {
      gchar *params[1];

      if (!separate_strings (params, G_N_ELEMENTS (params), detail))
        return;
      
      priv->progress = atof (params[0]);
      
      g_signal_emit (self, signals[PROGRESS], 0, priv->progress);
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
  else if (g_str_equal (command, "icon"))
    {
      g_free (priv->icon);
      priv->icon = NULL;

      if (!detail)
        return;

      priv->icon = g_strdup (detail);
      g_object_notify (G_OBJECT (self), "icon");
    }
  else if (g_str_equal (command, "net-start"))
    {
      priv->is_loading = TRUE;
      priv->progress = 0.0;
      g_signal_emit (self, signals[NET_START], 0);
    }
  else if (g_str_equal (command, "net-stop"))
    {
      priv->is_loading = FALSE;
      g_signal_emit (self, signals[NET_STOP], 0);
    }
  else if (g_str_equal (command, "back"))
    {
      gchar *params[1];
      gboolean back;

      if (!separate_strings (params, G_N_ELEMENTS (params), detail))
        return;

      back = atoi (params[0]);

      if (priv->can_go_back != back)
        {
          priv->can_go_back = back;
          g_object_notify (G_OBJECT (self), "can-go-back");
        }
    }
  else if (g_str_equal (command, "forward"))
    {
      gchar *params[1];
      gboolean forward;

      if (!separate_strings (params, G_N_ELEMENTS (params), detail))
        return;

      forward = atoi (params[0]);

      if (priv->can_go_forward != forward)
        {
          priv->can_go_forward = forward;
          g_object_notify (G_OBJECT (self), "can-go-forward");
        }
    }
  else if (g_str_equal (command, "new-window?"))
    {
      ClutterMozEmbed *new_window;
      gchar *params[1];

      if (!separate_strings (params, G_N_ELEMENTS (params), detail))
        return;

      new_window = g_object_new (CLUTTER_TYPE_MOZEMBED, "spawn", FALSE, NULL);

      /* Find out if the new window is received */
      g_object_ref_sink (new_window);
      g_object_add_weak_pointer (G_OBJECT (new_window), (gpointer)&new_window);
      g_signal_emit (self, signals[NEW_WINDOW], 0,
                     new_window, (guint)atoi (params[0]));
#ifdef SUPPORT_PLUGINS
      clutter_mozembed_init_viewport (new_window);
#endif
      g_object_unref (new_window);

      /* If it is, send its details to the backend */
      if (new_window)
        {
          gchar *output_file, *input_file, *shm_name, *command;

          g_object_remove_weak_pointer (G_OBJECT (new_window),
                                        (gpointer)&new_window);

          output_file = input_file = shm_name = NULL;
          g_object_get (G_OBJECT (new_window),
                        "output", &output_file,
                        "input", &input_file,
                        "shm", &shm_name,
                        NULL);

          command = g_strdup_printf ("new-window-response %s %s %s",
                                     input_file, output_file, shm_name);
          send_command (self, command);
          g_free (command);
        }
      else
        send_command (self, "new-window-response");
    }
  else if (g_str_equal (command, "closed"))
    {
      /* If we're in dispose, watch_id will be zero */
      if (priv->watch_id)
        g_signal_emit (self, signals[CLOSED], 0);
    }
  else if (g_str_equal (command, "link"))
    {
      g_signal_emit (self, signals[LINK_MESSAGE], 0, detail);
    }
  else if (g_str_equal (command, "size-request"))
    {
      gchar *params[2];

      if (!separate_strings (params, G_N_ELEMENTS (params), detail))
        return;

      g_signal_emit (self, signals[SIZE_REQUEST], 0,
                     atoi (params[0]), atoi (params[1]));
    }
  else if (g_str_equal (command, "shm-name"))
    {
      g_free (priv->shm_name);
      priv->shm_name = g_strdup (detail);
    }
  else if (g_str_equal (command, "cursor"))
    {
      gchar *params[1];
      int cursor;

      if (!separate_strings (params, G_N_ELEMENTS (params), detail))
        return;

      cursor = atoi (params[0]);

      priv->cursor = cursor;
      g_object_notify (G_OBJECT (self), "cursor");
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
  gboolean result = TRUE;

  if (condition & (G_IO_PRI | G_IO_IN))
    {
      GIOStatus status = g_io_channel_read_chars (source, buf, sizeof (buf),
                                                  &length, &error);
      if (status == G_IO_STATUS_NORMAL)
        {
          gsize current_length = 0;
          while (current_length < length)
            {
              gchar *feedback = &buf[current_length];
              current_length += strlen (&buf[current_length]) + 1;
              process_feedback (self, feedback);
            }
        }
      else if (status == G_IO_STATUS_ERROR)
        {
          g_warning ("Error reading from source: %s", error->message);
          g_error_free (error);
          result = FALSE;
        }
      else if (status == G_IO_STATUS_EOF)
        {
          g_warning ("Reached end of input pipe");
          result = FALSE;
        }
      /* do nothing if status is G_IO_STATUS_AGAIN */
    }

  if (condition & G_IO_HUP)
    {
      g_warning ("Unexpected hang-up");
      result = FALSE;
    }

  if (condition & (G_IO_ERR | G_IO_NVAL))
    {
      g_warning ("Error or invalid request");
      result = FALSE;
    }

  if (condition & ~(G_IO_PRI | G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL))
    {
      g_warning ("Unexpected IO condition");
      result = FALSE;
    }

  if (!result)
    g_signal_emit (self, signals[CRASHED], 0);

  return result;
}

void
block_until_feedback (ClutterMozEmbed *mozembed, const gchar *feedback)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;

  if (!priv->input)
    return;

  priv->sync_call = feedback;

  /* FIXME: There needs to be a time limit here, or we can hang if the backend
   *        hangs. Here or in input_io_func anyway...
   */
  while (input_io_func (priv->input, G_IO_IN, mozembed) && priv->sync_call);

  if (priv->sync_call)
    g_warning ("Error making synchronous call to backend");
}

#ifdef SUPPORT_PLUGINS
static int
error_handler (Display     *xdpy,
	       XErrorEvent *error)
{
  trapped_x_error = error->error_code;
  return 0;
}

void
clutter_mozembed_trap_x_errors (void)
{
  trapped_x_error = 0;
  prev_error_handler = XSetErrorHandler (error_handler);
}

int
clutter_mozembed_untrap_x_errors (void)
{
  XSetErrorHandler (prev_error_handler);
  return trapped_x_error;
}

static PluginWindow *
find_plugin_window (GList *plugins, Window xwindow)
{
  GList *pwin;
  for (pwin = plugins; pwin != NULL; pwin = pwin->next)
    {
      PluginWindow *plugin_window = pwin->data;
      Window        plugin_xwindow;

#ifndef DEBUG_PLUGIN_VIEWPORT
      g_object_get (G_OBJECT (plugin_window->plugin_tfp),
                    "window", &plugin_xwindow,
                    NULL);
#else
      return plugin_window;
#endif

      if (plugin_xwindow == xwindow)
        return plugin_window;
    }

  return NULL;
}

static void
clutter_mozembed_sync_plugin_viewport_pos (ClutterMozEmbed *mozembed)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;
  Display                *xdpy = clutter_x11_get_default_display ();
  ClutterGeometry         geom;
  gint                    abs_x, abs_y;
  gboolean                visible;
  gboolean                reactive;

  if (!priv->plugin_viewport)
    return;

  visible = CLUTTER_ACTOR_IS_VISIBLE (mozembed);
  reactive = CLUTTER_ACTOR_IS_REACTIVE (mozembed);

  clutter_actor_get_allocation_geometry (CLUTTER_ACTOR (mozembed), &geom);

  if (visible && reactive)
    {
      clutter_actor_get_transformed_position (CLUTTER_ACTOR (mozembed),
                                              &abs_x, &abs_y);

      XMoveWindow (xdpy, priv->plugin_viewport, abs_x, abs_y);

#warning "FIXME: plugin_viewport resizing needs to involve mozheadless"
      /* FIXME
       * Only mozheadless knows about scrollbars which affect the real
       * viewport size. For now we assume no horizontal scrollbar, and a
       * 50px vertical bar. */
      if (geom.width > 50 && geom.height > 0)
        {
#ifndef DEBUG_PLUGIN_VIEWPORT
          XResizeWindow (xdpy, priv->plugin_viewport,
                         geom.width - 50, geom.height);
#else /* XXX: Handy when disabling redirection of the viewport
         for debugging... */
          XResizeWindow (xdpy, priv->plugin_viewport,
                         geom.width - 200, geom.height - 200);
#endif
        }
    }
  else
    {
      /* Note we don't map/unmap the window since that would conflict with
       * xcomposite and live previews of plugins windows for hidden tabs */
      XMoveWindow (xdpy, priv->plugin_viewport, -geom.width, 0);
    }
}

static GdkFilterReturn
plugin_viewport_x_event_filter (GdkXEvent *gdk_xevent,
                                GdkEvent *event,
                                gpointer data)
{
  ClutterMozEmbed        *mozembed = CLUTTER_MOZEMBED (data);
  ClutterMozEmbedPrivate *priv = mozembed->priv;
  XEvent                 *xev = (XEvent *)gdk_xevent;
  Display                *xdpy = clutter_x11_get_default_display ();
  PluginWindow           *plugin_window;
  XWindowAttributes       attribs;
  Window                  parent_win;
  Window                  root_win;
  Window                 *children;
  unsigned int            n_children;
  Status                  status;

  switch (xev->type)
    {
    case MapNotify:

      clutter_mozembed_trap_x_errors ();
      status = XQueryTree (xdpy,
                      xev->xmap.window,
                      &root_win,
                      &parent_win,
                      &children,
                      &n_children);
      clutter_mozembed_untrap_x_errors ();
      if (status == 0)
        break;
      XFree (children);

      if (parent_win != priv->plugin_viewport)
        break;

      clutter_mozembed_trap_x_errors ();
      status = XGetWindowAttributes (xdpy, xev->xmap.window, &attribs);
      clutter_mozembed_untrap_x_errors ();
      if (status == 0)
        break;

      plugin_window = g_slice_new (PluginWindow);
      plugin_window->x = attribs.x;
      plugin_window->y = attribs.y;
#ifndef DEBUG_PLUGIN_VIEWPORT
      plugin_window->plugin_tfp =
        clutter_glx_texture_pixmap_new_with_window (xev->xmap.window);

      clutter_x11_texture_pixmap_set_automatic (
              CLUTTER_X11_TEXTURE_PIXMAP (plugin_window->plugin_tfp),
              TRUE);

      /* We don't want the parent (viewport) window to be automatically
       * updated with changes to the plugin window... */
      g_object_set (G_OBJECT (plugin_window->plugin_tfp),
                    "window-redirect-automatic",
                    FALSE,
                    NULL);
#else
      plugin_window->plugin_tfp = clutter_rectangle_new ();
      clutter_actor_set_size (plugin_window->plugin_tfp, 100, 100);
#endif

      clutter_actor_set_parent (plugin_window->plugin_tfp,
                                CLUTTER_ACTOR (mozembed));

      priv->plugin_windows =
        g_list_prepend (priv->plugin_windows, plugin_window);
      clutter_actor_queue_relayout (CLUTTER_ACTOR (mozembed));
      break;

    case UnmapNotify:
      plugin_window =
        find_plugin_window (priv->plugin_windows, xev->xunmap.window);
      if (!plugin_window)
        break;

      priv->plugin_windows =
        g_list_remove (priv->plugin_windows, plugin_window);
      clutter_actor_unparent (plugin_window->plugin_tfp);
      g_slice_free (PluginWindow, plugin_window);
      break;

    case ConfigureNotify:
      plugin_window =
        find_plugin_window (priv->plugin_windows, xev->xconfigure.window);
      if (!plugin_window)
        break;

      plugin_window->x = xev->xconfigure.x;
      plugin_window->y = xev->xconfigure.y;
      
      clutter_mozembed_sync_plugin_viewport_pos (mozembed);
      clutter_mozembed_allocate_plugins (mozembed, FALSE);
      break;
    }

  return  GDK_FILTER_CONTINUE;
}

static void
reactive_change_cb (GObject    *object,
                    GParamSpec *param_spec,
                    gpointer    data)
{
  clutter_mozembed_sync_plugin_viewport_pos (CLUTTER_MOZEMBED (object));
}

static gboolean
clutter_mozembed_init_viewport (ClutterMozEmbed *mozembed)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;
  Display *xdpy;
  ClutterActor *stage;
  unsigned long pixel;
  int composite_major, composite_minor;
  gchar *command;

  if (priv->plugin_viewport)
    return TRUE;

  if (priv->read_only)
    return FALSE;

  xdpy = clutter_x11_get_default_display ();
  if (!xdpy)
    return FALSE;

#ifdef DEBUG_PLUGIN_VIEWPORT
  XSynchronize (xdpy, True);
#endif

  stage = clutter_actor_get_stage (CLUTTER_ACTOR (mozembed));
  if (!stage || !CLUTTER_ACTOR_IS_REALIZED (stage))
    return FALSE;

  priv->stage_xwin = clutter_x11_get_stage_window (CLUTTER_STAGE (stage));
  if ((priv->stage_xwin == None) ||
      (priv->stage_xwin == clutter_x11_get_root_window()))
    return FALSE;

  priv->toplevel_window = gdk_window_foreign_new (priv->stage_xwin);
  if (!priv->toplevel_window)
    return FALSE;

  /* XXX: If you uncomment the call to XCompositeRedirectWindow below this can
   * simply help identify the viewport window for different tabs... */
#ifndef DEBUG_PLUGIN_VIEWPORT
  {
  static int color_toggle = 0;
  if (color_toggle)
    pixel = BlackPixel (xdpy, DefaultScreen (xdpy));
  else
    pixel = WhitePixel (xdpy, DefaultScreen (xdpy));
  color_toggle = !color_toggle;
  }
#else
  pixel = WhitePixel (xdpy, DefaultScreen (xdpy));
#endif

  /* NB: The plugin viewport will be repositioned within mozembed::allocate,
   * so the 100x100 size chosen here is arbitrary */
  priv->plugin_viewport =
    XCreateSimpleWindow (xdpy,
                         priv->stage_xwin,
                         -100, -100,
                         100, 100,
                         0, /* border width */
                         pixel, /* border color */
                         pixel); /* bg color */

#ifdef DEBUG_PLUGIN_VIEWPORT
  {
  XSetWindowAttributes attribs;
  attribs.background_pixel = 0xff0000;
  XChangeWindowAttributes (xdpy, priv->plugin_viewport, CWBackPixel, &attribs);
  }
#endif

  /* Note, unlike the individual plugin windows which we redirect using clutter,
   * we redirect the viewport window directly since we want to be sure we never
   * name a pixmap for it. NB: The viewport window is only needed for input
   * clipping.
   */
  if (XCompositeQueryVersion (xdpy, &composite_major, &composite_minor)
      != True)
    {
      g_critical ("The composite extension is required for redirecting "
                  "plugin windows");
    }
#ifndef DEBUG_PLUGIN_VIEWPORT
  XCompositeRedirectWindow (xdpy,
                            priv->plugin_viewport,
                            CompositeRedirectManual);
#endif
  XSelectInput (xdpy, priv->plugin_viewport,
                SubstructureNotifyMask);

  gdk_window_add_filter (NULL,
                         plugin_viewport_x_event_filter,
                         (gpointer)mozembed);

  XMapWindow (xdpy, priv->plugin_viewport);

  /* Make sure the window is mapped, or we can end up with unparented
   * plugin windows
   */
  XSync (xdpy, False);

  clutter_mozembed_sync_plugin_viewport_pos (mozembed);

  g_signal_connect (G_OBJECT (mozembed),
                    "notify::reactive",
                    G_CALLBACK (reactive_change_cb),
                    NULL);

  command = g_strdup_printf ("plugin-window %lu",
                             (gulong)priv->plugin_viewport);
  send_command (mozembed, command);
  g_free (command);

  if (priv->pending_open)
    clutter_mozembed_open (mozembed, priv->pending_url);

  return TRUE;
}

static void
clutter_mozembed_hide (ClutterActor *actor)
{
  CLUTTER_ACTOR_CLASS (clutter_mozembed_parent_class)->hide (actor);

  clutter_mozembed_sync_plugin_viewport_pos (CLUTTER_MOZEMBED (actor));
}
#endif

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

  case PROP_INPUT :
    g_value_set_string (value, self->priv->input_file);
    break;

  case PROP_OUTPUT :
    g_value_set_string (value, self->priv->output_file);
    break;

  case PROP_SHM :
    g_value_set_string (value, self->priv->shm_name);
    break;

  case PROP_ICON :
    g_value_set_string (value, clutter_mozembed_get_icon (self));
    break;

  case PROP_SMOOTH_SCROLL :
    g_value_set_boolean (value, clutter_mozembed_get_smooth_scroll (self));
    break;

  case PROP_DOC_WIDTH :
    g_value_set_int (value, self->priv->doc_width);
    break;

  case PROP_DOC_HEIGHT :
    g_value_set_int (value, self->priv->doc_height);
    break;

  case PROP_SCROLL_X :
    g_value_set_int (value, self->priv->scroll_x);
    break;

  case PROP_SCROLL_Y :
    g_value_set_int (value, self->priv->scroll_y);
    break;

  case PROP_POLL_TIMEOUT :
    g_value_set_uint (value, self->priv->poll_timeout);
    break;

  case PROP_CONNECT_TIMEOUT :
    g_value_set_uint (value, self->priv->connect_timeout);
    break;

  case PROP_CAN_GO_BACK :
    g_value_set_boolean (value, self->priv->can_go_back);
    break;

  case PROP_CAN_GO_FORWARD :
    g_value_set_boolean (value, self->priv->can_go_forward);
    break;

  case PROP_CURSOR :
    g_value_set_uint (value, self->priv->cursor);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
clutter_mozembed_set_property (GObject *object, guint property_id,
                              const GValue *value, GParamSpec *pspec)
{
  ClutterMozEmbed *self = CLUTTER_MOZEMBED (object);
  ClutterMozEmbedPrivate *priv = self->priv;

  switch (property_id) {
  case PROP_READONLY :
    priv->read_only = g_value_get_boolean (value);
    break;

  case PROP_INPUT :
    priv->input_file = g_value_dup_string (value);
    break;

  case PROP_OUTPUT :
    priv->output_file = g_value_dup_string (value);
    break;

  case PROP_SHM :
    priv->shm_name = g_value_dup_string (value);
    break;

  case PROP_SPAWN :
    priv->spawn = g_value_get_boolean (value);
    break;

  case PROP_SMOOTH_SCROLL :
    clutter_mozembed_set_smooth_scroll (self, g_value_get_boolean (value));
    break;

  case PROP_SCROLL_X :
    clutter_mozembed_scroll_by (self, g_value_get_int (value) -
                                      (priv->scroll_x + priv->offset_y), 0);
    break;

  case PROP_SCROLL_Y :
    clutter_mozembed_scroll_by (self, 0, g_value_get_int (value) -
                                         (priv->scroll_y + priv->offset_x));
    break;

  case PROP_POLL_TIMEOUT :
    priv->poll_timeout = g_value_get_uint (value);
    break;

  case PROP_CONNECT_TIMEOUT :
    priv->connect_timeout = g_value_get_uint (value);
    break;

  case PROP_CURSOR :
    priv->cursor = g_value_get_uint (value);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
disconnect_poll_sources (ClutterMozEmbed *self)
{
  ClutterMozEmbedPrivate *priv = self->priv;

  if (priv->poll_source)
    {
      g_source_remove (priv->poll_source);
      priv->poll_source = 0;
    }

  if (priv->poll_timeout_source)
    {
      g_source_remove (priv->poll_timeout_source);
      priv->poll_timeout_source = 0;
    }
}

gboolean
idle_monitor_unref_cb (gpointer data)
{
  g_object_unref (data);
  return FALSE;
}

static void
disconnect_file_monitor_sources (ClutterMozEmbed *self)
{
  ClutterMozEmbedPrivate *priv = self->priv;

  if (priv->monitor)
    {
      g_file_monitor_cancel (priv->monitor);
#ifndef DISABLE_GFILEMONITOR_BUG_WORKAROUND
      /* gio/gilemonitor.c:emit_cb does not cope if we dispose a monitor when
       * there are multiple pending_file_changes. */
      g_idle_add (idle_monitor_unref_cb, priv->monitor);
#else
      g_object_unref (priv->monitor);
#endif
      priv->monitor = NULL;
    }

  if (priv->watch_id)
    {
      g_source_remove (priv->watch_id);
      priv->watch_id = 0;
    }
}

static void
clutter_mozembed_dispose (GObject *object)
{
  ClutterMozEmbed *self = CLUTTER_MOZEMBED (object);
  ClutterMozEmbedPrivate *priv = self->priv;
#ifdef SUPPORT_PLUGINS
  Display                *xdpy = clutter_x11_get_default_display ();

  /* Note: we aren't using a saveset so this means any plugin
   * windows currently mapped here are coming down with us too!
   */
  if (priv->plugin_viewport)
    {
      Window             root_win;
      Window             parent_win;
      Window            *children;
      unsigned int       n_children;
      XWindowAttributes  attribs;
      Status             status;

      gdk_window_remove_filter (NULL,
                                plugin_viewport_x_event_filter,
                                (gpointer)object);

      clutter_mozembed_trap_x_errors ();
      status  = XQueryTree (xdpy,
                            priv->plugin_viewport,
                            &root_win,
                            &parent_win,
                            &children,
                            &n_children);
      clutter_mozembed_untrap_x_errors ();
      if (status != 0)
        {
          int           i;

          /* To potentially support re-attaching to a mozheadless backend with
           * plugin windows we could ask the mozheadless backend for a
           * temporary parent for the plugins instead of using the stage. */
          for (i = 0; i < n_children; i++)
            {
              int width = 1000;
              int height = 1000;

              clutter_mozembed_trap_x_errors ();

              if (XGetWindowAttributes (xdpy, children[i], &attribs) != 0)
                {
                  width = attribs.width;
                  height = attribs.height;
                }

              /* Make sure to position the plugin window offscreen so it doesn't
               * interfere with input. */
              /* XXX: There is the potential for the plugin window to be
               * resized and become visible within the stage. */
              XReparentWindow (xdpy,
                               children[i],
                               priv->stage_xwin,
                               -width,
                               -height);

              XSync (xdpy, False);
              clutter_mozembed_untrap_x_errors ();
            }

          XFree (children);
        }

      XDestroyWindow (xdpy, priv->plugin_viewport);
      priv->plugin_viewport = 0;
    }

  while (priv->plugin_windows)
    {
      PluginWindow *plugin_window = priv->plugin_windows->data;
      clutter_actor_unparent (plugin_window->plugin_tfp);
      g_slice_free (PluginWindow, plugin_window);
      priv->plugin_windows = g_list_delete_link (priv->plugin_windows,
                                                 priv->plugin_windows);
    }
#endif

  disconnect_poll_sources (self);
  disconnect_file_monitor_sources (self);

  if (priv->connect_timeout_source)
    {
      g_source_remove (priv->connect_timeout_source);
      priv->connect_timeout_source = 0;
    }

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
      GError *error = NULL;
      
      if (g_io_channel_shutdown (priv->output, FALSE, &error) ==
          G_IO_STATUS_ERROR)
        {
          g_warning ("Error closing IO channel: %s", error->message);
          g_error_free (error);
        }
      
      g_io_channel_unref (priv->output);
      priv->output = NULL;
    }

  G_OBJECT_CLASS (clutter_mozembed_parent_class)->dispose (object);
}

static void
clutter_mozembed_finalize (GObject *object)
{
  ClutterMozEmbedPrivate *priv = CLUTTER_MOZEMBED (object)->priv;
  
  g_remove (priv->output_file);
  g_remove (priv->input_file);
  
  g_free (priv->location);
  g_free (priv->title);
  g_free (priv->input_file);
  g_free (priv->output_file);
  g_free (priv->shm_name);
#ifdef SUPPORT_PLUGINS
  g_free (priv->pending_url);
#endif

  if (priv->image_data)
    munmap (priv->image_data, priv->image_size);
  
  G_OBJECT_CLASS (clutter_mozembed_parent_class)->finalize (object);
}

#ifdef SUPPORT_PLUGINS
static void
clutter_mozembed_allocate_plugins (ClutterMozEmbed *mozembed,
                                   gboolean         absolute_origin_changed)
{
  GList *pwin;

  ClutterMozEmbedPrivate *priv = mozembed->priv;

  for (pwin = priv->plugin_windows; pwin != NULL; pwin = pwin->next)
    {
      PluginWindow   *plugin_window = pwin->data;
      ClutterActor   *plugin_tfp = CLUTTER_ACTOR (plugin_window->plugin_tfp);
      ClutterUnit     natural_width, natural_height;
      ClutterActorBox child_box;

      /* Note, the texture-from-pixmap actor is considered authorative over
       * its width and height as opposed to us tracking the width and height
       * of the actor according to configure notify events of the corresponding
       * plugin window.
       *
       * If the tfp actor hasn't yet renamed the pixmap for the redirected
       * window since it was last resized, then I guess it will look better if
       * we avoid scaling whatever pixmap we currently have. */
      clutter_actor_get_preferred_size (plugin_tfp,
                                        NULL, NULL,
                                        &natural_width, &natural_height);

      child_box.x1 = plugin_window->x;
      child_box.y1 = plugin_window->y;
      child_box.x2 = plugin_window->x + natural_width;
      child_box.y2 = plugin_window->y + natural_height;

      clutter_actor_allocate (plugin_window->plugin_tfp,
                              &child_box,
                              absolute_origin_changed);
    }
}
#endif

static void
clutter_mozembed_get_preferred_width (ClutterActor *actor,
                                      ClutterUnit   for_height,
                                      ClutterUnit  *min_width_p,
                                      ClutterUnit  *natural_width_p)
{
  ClutterMozEmbed *mozembed = CLUTTER_MOZEMBED (actor);
  ClutterMozEmbedPrivate *priv = mozembed->priv;

  if (priv->read_only)
    {
      gint width, height;

      if (min_width_p)
        *min_width_p = 0;

      clutter_texture_get_base_size (CLUTTER_TEXTURE (actor),
                                     &width, &height);

      if (natural_width_p)
        {
          if (for_height > 0)
            *natural_width_p = width / (gfloat)height * for_height;
          else
            *natural_width_p = CLUTTER_UNITS_FROM_INT (width);
        }
    }
  else
    CLUTTER_ACTOR_CLASS (clutter_mozembed_parent_class)->
      get_preferred_width (actor, for_height, min_width_p, natural_width_p);
}

static void
clutter_mozembed_get_preferred_height (ClutterActor *actor,
                                      ClutterUnit    for_width,
                                      ClutterUnit   *min_height_p,
                                      ClutterUnit   *natural_height_p)
{
  ClutterMozEmbed *mozembed = CLUTTER_MOZEMBED (actor);
  ClutterMozEmbedPrivate *priv = mozembed->priv;

  if (priv->read_only)
    {
      gint width, height;

      if (min_height_p)
        *min_height_p = 0;

      clutter_texture_get_base_size (CLUTTER_TEXTURE (actor),
                                     &width, &height);

      if (natural_height_p)
        {
          if (for_width > 0)
            *natural_height_p = height / (gfloat)width * for_width;
          else
            *natural_height_p = CLUTTER_UNITS_FROM_INT (height);
        }
    }
  else
    CLUTTER_ACTOR_CLASS (clutter_mozembed_parent_class)->
      get_preferred_height (actor, for_width, min_height_p, natural_height_p);
}

static void
clutter_mozembed_allocate (ClutterActor          *actor,
                           const ClutterActorBox *box,
                           gboolean               absolute_origin_changed)
{
  gchar *command;
  gint width, height, tex_width, tex_height;
  ClutterMozEmbed *mozembed = CLUTTER_MOZEMBED (actor);
  ClutterMozEmbedPrivate *priv = mozembed->priv;

  width = CLUTTER_UNITS_TO_INT (box->x2 - box->x1);
  height = CLUTTER_UNITS_TO_INT (box->y2 - box->y1);
  
  if (width < 0 || height < 0)
    return;

  clutter_texture_get_base_size (CLUTTER_TEXTURE (actor),
                                 &tex_width, &tex_height);
  
  if ((!priv->read_only) && ((tex_width != width) || (tex_height != height)))
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

#ifdef SUPPORT_PLUGINS
  if (priv->plugin_viewport)
    {
      clutter_mozembed_sync_plugin_viewport_pos (mozembed);
      clutter_mozembed_allocate_plugins (mozembed, absolute_origin_changed);
    }
  else
    {
      /* We want an actor-mapped signal, but until then, this is a
       * hacky way of ensuring that we're on a stage.
       */
      if (!clutter_mozembed_init_viewport (mozembed))
        g_warning ("Failed to initialise plugin window");
    }
#endif
}

static void
clutter_mozembed_paint (ClutterActor *actor)
{
  ClutterMozEmbed *self = CLUTTER_MOZEMBED (actor);
  ClutterMozEmbedPrivate *priv = self->priv;
  ClutterGeometry geom;
#ifdef SUPPORT_PLUGINS
  GList *pwin;
#endif

  clutter_actor_get_allocation_geometry (actor, &geom);

  /* Offset if we're using async scrolling */
  if (priv->async_scroll)
    {
      cogl_clip_push (0, 0, geom.width, geom.height);
      cogl_translate (priv->offset_x, priv->offset_y, 0);
    }

  /* Paint texture */
  CLUTTER_ACTOR_CLASS (clutter_mozembed_parent_class)->paint (actor);

#ifdef SUPPORT_PLUGINS
  /* Paint plugin windows */
  cogl_clip_push (0, 0, geom.width, geom.height);
  for (pwin = priv->plugin_windows; pwin != NULL; pwin = pwin->next)
    {
      PluginWindow *plugin_window = pwin->data;
      ClutterActor *plugin_tfp = CLUTTER_ACTOR (plugin_window->plugin_tfp);

      if (CLUTTER_ACTOR_IS_VISIBLE (plugin_tfp))
        clutter_actor_paint (plugin_tfp);
    }
  cogl_clip_pop ();
#endif

  if (priv->async_scroll)
    cogl_clip_pop ();

  if (priv->new_data && CLUTTER_ACTOR_IS_VISIBLE (actor))
    {
      priv->new_data = FALSE;
      send_command (self, "ack!");
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
      priv->pending_motion = TRUE;
      return TRUE;
    }
  
  send_motion_event (CLUTTER_MOZEMBED (actor));
  
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

  if (priv->async_scroll)
    {
      /* FIXME: Maybe we shouldn't do this? */
      switch (event->direction)
        {
        case CLUTTER_SCROLL_UP :
          clutter_mozembed_scroll_by (CLUTTER_MOZEMBED (actor), 0, -50);
          break;
        default:
        case CLUTTER_SCROLL_DOWN :
          clutter_mozembed_scroll_by (CLUTTER_MOZEMBED (actor), 0, 50);
          break;
        case CLUTTER_SCROLL_LEFT :
          clutter_mozembed_scroll_by (CLUTTER_MOZEMBED (actor), -50, 0);
          break;
        case CLUTTER_SCROLL_RIGHT :
          clutter_mozembed_scroll_by (CLUTTER_MOZEMBED (actor), 50, 0);
          break;
        }
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
file_changed_cb (GFileMonitor      *monitor,
                 GFile             *file,
                 GFile             *other_file,
                 GFileMonitorEvent  event_type,
                 ClutterMozEmbed   *self)
{
  gint fd;
  ClutterMozEmbedPrivate *priv = self->priv;

  if (event_type != G_FILE_MONITOR_EVENT_CREATED)
    return;

  disconnect_poll_sources (self);
  disconnect_file_monitor_sources (self);

  if (priv->connect_timeout_source)
    {
      g_source_remove (priv->connect_timeout_source);
      priv->connect_timeout_source = 0;
    }

  /* Open input channel */
  fd = open (priv->output_file, O_RDONLY | O_NONBLOCK);
  priv->input = g_io_channel_unix_new (fd);
  g_io_channel_set_encoding (priv->input, NULL, NULL);
  g_io_channel_set_buffered (priv->input, FALSE);
  g_io_channel_set_close_on_unref (priv->input, TRUE);
  priv->watch_id = g_io_add_watch (priv->input,
                                   G_IO_IN | G_IO_PRI | G_IO_ERR |
                                   G_IO_NVAL | G_IO_HUP,
                                   (GIOFunc)input_io_func,
                                   self);
}

static gboolean
poll_idle_cb (ClutterMozEmbed *self)
{
  ClutterMozEmbedPrivate *priv = self->priv;

  if (g_file_test (priv->output_file, G_FILE_TEST_EXISTS))
    {
      file_changed_cb (priv->monitor, NULL, NULL,
                       G_FILE_MONITOR_EVENT_CREATED, self);
      return FALSE;
    }

  return TRUE;
}

static gboolean
poll_timeout_cb (ClutterMozEmbed *self)
{
  disconnect_poll_sources (self);
  return FALSE;
}

static void
clutter_mozembed_open_pipes (ClutterMozEmbed *self)
{
  gint fd;
  GFile *file;
  
  GError *error = NULL;
  ClutterMozEmbedPrivate *priv = self->priv;

  /* FIXME: This needs a time-out, or we can block here indefinitely if the
   *        backend crashes.
   */

  /* Wait for headless process to create the output pipe */
  file = g_file_new_for_path (priv->output_file);
  priv->monitor = g_file_monitor_file (file, 0, NULL, &error);
  if (!priv->monitor)
    {
      g_warning ("Error creating file monitor: %s", error->message);
      g_error_free (error);
    }
  g_object_unref (file);
  g_signal_connect (priv->monitor, "changed",
                    G_CALLBACK (file_changed_cb), self);

  if (g_file_test (priv->output_file, G_FILE_TEST_EXISTS))
    file_changed_cb (priv->monitor, NULL, NULL,
                     G_FILE_MONITOR_EVENT_CREATED, self);
  else
    {
      if (priv->poll_timeout)
        {
          priv->poll_source = g_idle_add ((GSourceFunc)poll_idle_cb,
                                          self);
          priv->poll_timeout_source =
            g_timeout_add (priv->poll_timeout,
                           (GSourceFunc)poll_timeout_cb,
                           self);
        }
    }

  /* Open output channel */
  mkfifo (priv->input_file, S_IWUSR | S_IRUSR);
  fd = open (priv->input_file, O_RDWR | O_NONBLOCK);
  priv->output = g_io_channel_unix_new (fd);
  g_io_channel_set_encoding (priv->output, NULL, NULL);
  g_io_channel_set_buffered (priv->output, FALSE);
  g_io_channel_set_close_on_unref (priv->output, TRUE);
}

static gboolean
connect_timeout_cb (ClutterMozEmbed *self)
{
  disconnect_poll_sources (self);
  disconnect_file_monitor_sources (self);
  g_signal_emit (self, signals[CRASHED], 0);
  return FALSE;
}

static void
clutter_mozembed_constructed (GObject *object)
{
  static gint spawned_windows = 0;
  gboolean success;

  gchar *argv[] = {
    "clutter-mozheadless",  /* TODO: Should probably use an absolute path.. */
    NULL, /* Output pipe */
    NULL, /* Input pipe */
    NULL, /* SHM name */
    NULL
  };

  ClutterMozEmbed *self = CLUTTER_MOZEMBED (object);
  ClutterMozEmbedPrivate *priv = self->priv;
  GError *error = NULL;

  /* Set up out-of-process renderer */
  
  /* Generate names for pipe/shm, if not provided */
  /* Don't overwrite user-supplied pipe files */
  if (priv->output_file && g_file_test (priv->output_file, G_FILE_TEST_EXISTS))
    {
      g_free (priv->output_file);
      priv->output_file = NULL;
    }
  if (g_file_test (priv->input_file, G_FILE_TEST_EXISTS))
    {
      g_free (priv->input_file);
      priv->input_file = NULL;
    }
  
  if (!priv->output_file)
    priv->output_file = g_strdup_printf ("%s/clutter-mozembed-%d-%d",
                                         g_get_tmp_dir (), getpid (),
                                         spawned_windows);
  
  if (!priv->input_file)
    priv->input_file = g_strdup_printf ("%s/clutter-mozheadless-%d-%d",
                                        g_get_tmp_dir (), getpid (),
                                        spawned_windows);
  
  if (!priv->shm_name)
    priv->shm_name = g_strdup_printf ("/mozheadless-%d-%d",
                                      getpid (), spawned_windows);
  
  spawned_windows++;
  
  /* Spawn renderer */
  argv[1] = priv->output_file;
  argv[2] = priv->input_file;
  argv[3] = priv->shm_name;

  if (priv->spawn)
    {
      if (g_getenv ("CLUTTER_MOZEMBED_DEBUG"))
        {
          g_message ("Waiting for '%s %s %s %s' to be run",
                     argv[0], argv[1], argv[2], argv[3]);
          priv->connect_timeout = 0;
        }
      else
        {
          success = g_spawn_async_with_pipes (NULL,
                                              argv,
                                              NULL,
                                              G_SPAWN_SEARCH_PATH/* |
                                              G_SPAWN_STDERR_TO_DEV_NULL |
                                              G_SPAWN_STDOUT_TO_DEV_NULL*/,
                                              NULL,
                                              NULL,
                                              &priv->child_pid,
                                              NULL,
                                              NULL,
                                              NULL,
                                              &error);
          
          if (!success)
            {
              g_warning ("Error spawning renderer: %s", error->message);
              g_error_free (error);
              return;
            }
        }
    }

  if (priv->connect_timeout)
    priv->connect_timeout_source =
      g_timeout_add (priv->connect_timeout,
                     (GSourceFunc)connect_timeout_cb,
                     self);

  clutter_mozembed_open_pipes (self);
}

static void
clutter_mozembed_size_change (ClutterTexture *texture, gint width, gint height)
{
  ClutterMozEmbedPrivate *priv = CLUTTER_MOZEMBED (texture)->priv;
  
  if (priv->read_only)
    clutter_actor_queue_relayout (CLUTTER_ACTOR (texture));
}

static void
clutter_mozembed_class_init (ClutterMozEmbedClass *klass)
{
  GObjectClass      *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  ClutterTextureClass *texture_class = CLUTTER_TEXTURE_CLASS (klass);

  g_type_class_add_private (klass, sizeof (ClutterMozEmbedPrivate));

  object_class->get_property = clutter_mozembed_get_property;
  object_class->set_property = clutter_mozembed_set_property;
  object_class->dispose = clutter_mozembed_dispose;
  object_class->finalize = clutter_mozembed_finalize;
  object_class->constructed = clutter_mozembed_constructed;
  
  actor_class->allocate             = clutter_mozembed_allocate;
  actor_class->get_preferred_width  = clutter_mozembed_get_preferred_width;
  actor_class->get_preferred_height = clutter_mozembed_get_preferred_height;
  actor_class->paint                = clutter_mozembed_paint;
  actor_class->pick                 = clutter_mozembed_pick;
  actor_class->motion_event         = clutter_mozembed_motion_event;
  actor_class->button_press_event   = clutter_mozembed_button_press_event;
  actor_class->button_release_event = clutter_mozembed_button_release_event;
  actor_class->key_press_event      = clutter_mozembed_key_press_event;
  actor_class->key_release_event    = clutter_mozembed_key_release_event;
  actor_class->scroll_event         = clutter_mozembed_scroll_event;
#ifdef SUPPORT_PLUGINS
  actor_class->hide                 = clutter_mozembed_hide;
#endif

  texture_class->size_change        = clutter_mozembed_size_change;

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
                                   PROP_INPUT,
                                   g_param_spec_string ("input",
                                                        "Input file",
                                                        "Communications pipe "
                                                        "file name (input).",
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB |
                                                        G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (object_class,
                                   PROP_OUTPUT,
                                   g_param_spec_string ("output",
                                                        "Output file",
                                                        "Communications pipe "
                                                        "file name (output).",
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

  g_object_class_install_property (object_class,
                                   PROP_SPAWN,
                                   g_param_spec_boolean ("spawn",
                                                         "Spawn",
                                                         "Spawn a new process.",
                                                         TRUE,
                                                         G_PARAM_WRITABLE |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_NICK |
                                                         G_PARAM_STATIC_BLURB |
                                                         G_PARAM_CONSTRUCT_ONLY));


  g_object_class_install_property (object_class,
                                   PROP_ICON,
                                   g_param_spec_string ("icon",
                                                        "Icon",
                                                        "URL to the icon "
                                                        "for this page",
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_SMOOTH_SCROLL,
                                   g_param_spec_boolean ("smooth-scroll",
                                                         "Smooth scrolling",
                                                         "Use asynchronous "
                                                         "(smooth) scrolling.",
                                                         FALSE,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_NICK |
                                                         G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_DOC_WIDTH,
                                   g_param_spec_int ("doc-width",
                                                     "Document width",
                                                     "Width of the document.",
                                                     0, G_MAXINT, 0,
                                                     G_PARAM_READABLE |
                                                     G_PARAM_STATIC_NAME |
                                                     G_PARAM_STATIC_NICK |
                                                     G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_DOC_HEIGHT,
                                   g_param_spec_int ("doc-height",
                                                     "Document height",
                                                     "Height of the document.",
                                                     0, G_MAXINT, 0,
                                                     G_PARAM_READABLE |
                                                     G_PARAM_STATIC_NAME |
                                                     G_PARAM_STATIC_NICK |
                                                     G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_SCROLL_X,
                                   g_param_spec_int ("scroll-x",
                                                     "Scroll X",
                                                     "Current X-axis scroll "
                                                     "position.",
                                                     0, G_MAXINT, 0,
                                                     G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_NAME |
                                                     G_PARAM_STATIC_NICK |
                                                     G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_SCROLL_Y,
                                   g_param_spec_int ("scroll-y",
                                                     "Scroll Y",
                                                     "Current Y-axis scroll "
                                                     "position.",
                                                     0, G_MAXINT, 0,
                                                     G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_NAME |
                                                     G_PARAM_STATIC_NICK |
                                                     G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_POLL_TIMEOUT,
                                   g_param_spec_uint ("poll-timeout",
                                                      "Poll-timeout",
                                                      "Amount of time to "
                                                      "try polling for a "
                                                      "connection (in ms).",
                                                      0, G_MAXINT, 1000,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_NAME |
                                                      G_PARAM_STATIC_NICK |
                                                      G_PARAM_STATIC_BLURB |
                                                      G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (object_class,
                                   PROP_CONNECT_TIMEOUT,
                                   g_param_spec_uint ("connect-timeout",
                                                      "Connect-timeout",
                                                      "Amount of time to "
                                                      "wait for a "
                                                      "connection (in ms).",
                                                      0, G_MAXINT, 10000,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_NAME |
                                                      G_PARAM_STATIC_NICK |
                                                      G_PARAM_STATIC_BLURB |
                                                      G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (object_class,
                                   PROP_CAN_GO_BACK,
                                   g_param_spec_boolean ("can-go-back",
                                                         "Can go back",
                                                         "Whether backwards "
                                                         "navigation is "
                                                         "possible.",
                                                         FALSE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_NICK |
                                                         G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_CAN_GO_FORWARD,
                                   g_param_spec_boolean ("can-go-forward",
                                                         "Can go forward",
                                                         "Whether forwards "
                                                         "navigation is "
                                                         "possible.",
                                                         FALSE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_NICK |
                                                         G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class,
                                   PROP_CURSOR,
                                   g_param_spec_uint ("cursor",
                                                      "Cursor",
                                                      "Cursor type",
                                                      0, G_MAXINT, 0,
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

  signals[CRASHED] =
    g_signal_new ("crashed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterMozEmbedClass, crashed),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[NEW_WINDOW] =
    g_signal_new ("new-window",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterMozEmbedClass, new_window),
                  NULL, NULL,
                  _clutter_mozembed_marshal_VOID__OBJECT_UINT,
                  G_TYPE_NONE, 2, CLUTTER_TYPE_MOZEMBED, G_TYPE_UINT);

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterMozEmbedClass, closed),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[LINK_MESSAGE] =
    g_signal_new ("link-message",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterMozEmbedClass, link_message),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__STRING,
                  G_TYPE_NONE, 1, G_TYPE_STRING);

  signals[SIZE_REQUEST] =
    g_signal_new ("size-request",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterMozEmbedClass, size_request),
                  NULL, NULL,
                  _clutter_mozembed_marshal_VOID__INT_INT,
                  G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);
}

static void
clutter_mozembed_init (ClutterMozEmbed *self)
{
  ClutterMozEmbedPrivate *priv = self->priv = MOZEMBED_PRIVATE (self);
 
  priv->motion_ack = TRUE;
  priv->spawn = TRUE;
  priv->poll_timeout = 1000;
  priv->connect_timeout = 10000;

  clutter_actor_set_reactive (CLUTTER_ACTOR (self), TRUE);
  
  /* Turn off sync-size (we manually size the texture on allocate) */
  g_object_set (G_OBJECT (self), "sync-size", FALSE, NULL);
}

ClutterActor *
clutter_mozembed_new (void)
{
  return CLUTTER_ACTOR (g_object_new (CLUTTER_TYPE_MOZEMBED, NULL));
}

ClutterActor *
clutter_mozembed_new_with_parent (ClutterMozEmbed *parent)
{
  gchar *input, *output, *shm, *command;
  ClutterActor *mozembed;

  if (!parent)
    return clutter_mozembed_new ();

  /* Open up a new window using the same process as the provided
   * ClutterMozEmbed.
   */
  mozembed = g_object_new (CLUTTER_TYPE_MOZEMBED, "spawn", FALSE, NULL);
  g_object_get (G_OBJECT (mozembed),
                "input", &input,
                "output", &output,
                "shm", &shm,
                NULL);

  command = g_strdup_printf ("new-window %s %s %s", input, output, shm);
  send_command (parent, command);
  g_free (command);
  
  return CLUTTER_ACTOR (mozembed);
}

ClutterActor *
clutter_mozembed_new_view (void)
{
  ClutterMozEmbed *mozembed;

 /* Create a read-only mozembed */
  mozembed = g_object_new (CLUTTER_TYPE_MOZEMBED,
                           "read-only", TRUE,
                           "spawn", FALSE,
                           NULL);

  return CLUTTER_ACTOR (mozembed);
}

void
clutter_mozembed_connect_view (ClutterMozEmbed *mozembed,
                               const gchar     *input,
                               const gchar     *output)
{
  gchar *command;

  command = g_strdup_printf ("new-view %s %s", input, output);
  send_command (mozembed, command);
  g_free (command);
}

void
clutter_mozembed_open (ClutterMozEmbed *mozembed, const gchar *uri)
{
#ifdef SUPPORT_PLUGINS
  ClutterMozEmbedPrivate *priv = mozembed->priv;

  if (!clutter_mozembed_init_viewport (mozembed))
    {
      priv->pending_open = TRUE;
      priv->pending_url = g_strdup (uri);
      return;
    }
#endif

  gchar *command = g_strdup_printf ("open %s", uri);
  send_command (mozembed, command);
  g_free (command);

#ifdef SUPPORT_PLUGINS
  if (priv->pending_open)
    {
      priv->pending_open = FALSE;
      g_free (priv->pending_url);
      priv->pending_url = NULL;
    }
#endif
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

const gchar *
clutter_mozembed_get_icon (ClutterMozEmbed *mozembed)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;
  return priv->icon;
}

gboolean
clutter_mozembed_can_go_back (ClutterMozEmbed *mozembed)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;
  return priv->can_go_back;
}

gboolean
clutter_mozembed_can_go_forward (ClutterMozEmbed *mozembed)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;
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

gboolean
clutter_mozembed_get_smooth_scroll (ClutterMozEmbed *mozembed)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;
  return priv->async_scroll;
}

void
clutter_mozembed_set_smooth_scroll (ClutterMozEmbed *mozembed, gboolean smooth)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;

  if (priv->async_scroll != smooth)
    {
      gchar *command;
      priv->async_scroll = smooth;
      
      command = g_strdup_printf ("toggle-chrome %d",
                                 MOZ_HEADLESS_FLAG_SCROLLBARSON);
      send_command (mozembed, command);
      g_free (command);
    }
}

void
clutter_mozembed_scroll_by (ClutterMozEmbed *mozembed, gint dx, gint dy)
{
  gchar *command;

  ClutterMozEmbedPrivate *priv = mozembed->priv;

  command = g_strdup_printf ("scroll %d %d", dx, dy);
  send_command (mozembed, command);
  g_free (command);

  priv->offset_x -= dx;
  priv->offset_y -= dy;

  clamp_offset (mozembed);
  clutter_actor_queue_redraw (CLUTTER_ACTOR (mozembed));
}

gboolean
clutter_mozembed_is_loading (ClutterMozEmbed *mozembed)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;
  return priv->is_loading;
}

gdouble
clutter_mozembed_get_progress (ClutterMozEmbed *mozembed)
{
  ClutterMozEmbedPrivate *priv = mozembed->priv;
  return priv->progress;
}

MozHeadlessCursorType
clutter_mozembed_get_cursor (ClutterMozEmbed *mozembed)
{
  return mozembed->priv->cursor;
}
