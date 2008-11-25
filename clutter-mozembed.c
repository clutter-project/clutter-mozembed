#include "clutter-mozembed.h"
#include <mozheadless/moz-headless.h>
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


struct _ClutterMozEmbedPrivate
{
  GIOChannel      *input;
  FILE            *output;
  
  gboolean         motion_ack;
  gulong           motion_throttle;
  gint             motion_x;
  gint             motion_y;
  
  ClutterActor    *texture;
  gboolean         opened_shm;
  gboolean         new_data;
  int              shm_fd;
  
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
  ClutterTexture *texture = CLUTTER_TEXTURE (priv->texture);
  gint surface_width, surface_height;
  
  if (!priv->opened_shm)
    {
      /*g_debug ("Opening shm");*/
      priv->opened_shm = TRUE;
      priv->shm_fd = shm_open ("/mozheadless", O_RDONLY, 0444);
      if (priv->shm_fd == -1)
        g_error ("Error opening shared memory");
    }
  
  clutter_texture_get_base_size (texture, &surface_width, &surface_height);
  
  if (!priv->image_data)
    {
      /*g_debug ("mmapping data");*/
      priv->image_size = surface_width * surface_height * 4;
      priv->image_data = mmap (NULL, priv->image_size, PROT_READ,
                               MAP_SHARED, priv->shm_fd, 0);
    }
  
  /*g_debug ("Reading data");*/
  clutter_texture_set_area_from_rgb_data (texture,
                                          priv->image_data +
                                          (x * 4) +
                                          (y * surface_width * 4),
                                          TRUE,
                                          x,
                                          y,
                                          width,
                                          height,
                                          surface_width * 4,
                                          4,
                                          CLUTTER_TEXTURE_RGB_FLAG_BGR,
                                          NULL);
}

static void
process_feedback (ClutterMozEmbed *self, const gchar *command)
{
  gchar *detail;
  
  ClutterMozEmbedPrivate *priv = self->priv;
  
  detail = strchr (command, ' ');
  if (detail)
    {
      detail[0] = '\0';
      detail++;
    }

  if (strcmp (command, "update") == 0)
    {
      gint x, y, width, height;
      
      gchar *params[4];
      if (!separate_strings (params, 4, detail))
        return;
      
      x = atoi (params[0]);
      y = atoi (params[1]);
      width = atoi (params[2]);
      height = atoi (params[3]);
      
      priv->new_data = TRUE;
      
      update (self, x, y, width, height);
      
      clutter_actor_queue_redraw (CLUTTER_ACTOR (self));
    }
  else if (strcmp (command, "mack") == 0)
    {
      priv->motion_ack = TRUE;
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
  
  fwrite (command, strlen (command) + 1, 1, mozembed->priv->output);
  fflush (mozembed->priv->output);
}

static void
clutter_mozembed_get_property (GObject *object, guint property_id,
                              GValue *value, GParamSpec *pspec)
{
  switch (property_id) {
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
  G_OBJECT_CLASS (clutter_mozembed_parent_class)->dispose (object);
}

static void
clutter_mozembed_finalize (GObject *object)
{
  ClutterMozEmbedPrivate *priv = CLUTTER_MOZEMBED (object)->priv;
  
  if (priv->image_data)
    munmap (priv->image_data, priv->image_size);
  
  G_OBJECT_CLASS (clutter_mozembed_parent_class)->finalize (object);
}

static void
clutter_mozembed_get_preferred_width (ClutterActor *actor,
                                      ClutterUnit   for_height,
                                      ClutterUnit  *min_width_p,
                                      ClutterUnit  *natural_width_p)
{
  ClutterMozEmbedPrivate *priv = CLUTTER_MOZEMBED (actor)->priv;
  clutter_actor_get_preferred_width (priv->texture,
                                     for_height,
                                     min_width_p,
                                     natural_width_p);
}

static void
clutter_mozembed_get_preferred_height (ClutterActor *actor,
                                       ClutterUnit   for_width,
                                       ClutterUnit  *min_height_p,
                                       ClutterUnit  *natural_height_p)
{
  ClutterMozEmbedPrivate *priv = CLUTTER_MOZEMBED (actor)->priv;
  clutter_actor_get_preferred_height (priv->texture,
                                      for_width,
                                      min_height_p,
                                      natural_height_p);
}

static void
clutter_mozembed_allocate (ClutterActor          *actor,
                           const ClutterActorBox *box,
                           gboolean               absolute_origin_changed)
{
  gchar *command;
  gint width, height, tex_width, tex_height;
  ClutterActorBox child_box;
  ClutterMozEmbedPrivate *priv = CLUTTER_MOZEMBED (actor)->priv;
  
  child_box.x1 = 0;
  child_box.y1 = 0;
  child_box.x2 = box->x2 - box->x1;
  child_box.y2 = box->y2 - box->y1;
  
  clutter_actor_allocate (priv->texture, &child_box, absolute_origin_changed);

  width = CLUTTER_UNITS_TO_INT (child_box.x2);
  height = CLUTTER_UNITS_TO_INT (child_box.y2);
  
  clutter_texture_get_base_size (CLUTTER_TEXTURE (priv->texture),
                                 &tex_width, &tex_height);
  
  if ((tex_width != width) || (tex_height != height))
    {
      guchar *data = g_slice_alloc (width * height * 4);
      clutter_texture_set_from_rgb_data (CLUTTER_TEXTURE (priv->texture),
                                         data,
                                         TRUE,
                                         width,
                                         height,
                                         width * 4,
                                         4,
                                         CLUTTER_TEXTURE_RGB_FLAG_BGR,
                                         NULL);
      g_slice_free1 (width * height * 4, data);
    }
  
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
  
  clutter_actor_paint (priv->texture);
  
  if (priv->new_data)
    {
      priv->new_data = FALSE;
      send_command (self, "ack");
    }
}

static void
clutter_mozembed_pick (ClutterActor *actor, const ClutterColor *color)
{
  clutter_mozembed_paint (actor);
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
clutter_mozembed_key_press_event (ClutterActor *actor, ClutterKeyEvent *event)
{
  gchar *command = g_strdup_printf ("key-press %d", event->keyval);
  send_command (CLUTTER_MOZEMBED (actor), command);
  g_free (command);
  
  return TRUE;
}

static gboolean
clutter_mozembed_key_release_event (ClutterActor *actor, ClutterKeyEvent *event)
{
  gchar *command = g_strdup_printf ("key-release %d", event->keyval);
  send_command (CLUTTER_MOZEMBED (actor), command);
  g_free (command);
  
  return TRUE;
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
  
  actor_class->get_preferred_width  = clutter_mozembed_get_preferred_width;
  actor_class->get_preferred_height = clutter_mozembed_get_preferred_height;
  actor_class->allocate             = clutter_mozembed_allocate;
  actor_class->paint                = clutter_mozembed_paint;
  actor_class->pick                 = clutter_mozembed_pick;
  actor_class->motion_event         = clutter_mozembed_motion_event;
  actor_class->button_press_event   = clutter_mozembed_button_press_event;
  actor_class->button_release_event = clutter_mozembed_button_release_event;
  actor_class->key_press_event      = clutter_mozembed_key_press_event;
  actor_class->key_release_event    = clutter_mozembed_key_release_event;
  actor_class->scroll_event         = clutter_mozembed_scroll_event;
}

static void
clutter_mozembed_init (ClutterMozEmbed *self)
{
  gint standard_input;
  gboolean success;

  gchar *argv[] = {
    "mozheadless",
    /* FIXME: We need to dynamically generate a name in /tmp or something */
    "./mozheadless-pipe",
    NULL
  };

  GError *error = NULL;

  ClutterMozEmbedPrivate *priv = self->priv = MOZEMBED_PRIVATE (self);
  
  priv->motion_ack = TRUE;

  priv->texture = clutter_texture_new ();
  clutter_actor_show (priv->texture);
  clutter_actor_set_parent (priv->texture, CLUTTER_ACTOR (self));
  
  clutter_actor_set_reactive (CLUTTER_ACTOR (self), TRUE);
  clutter_actor_set_reactive (priv->texture, TRUE);
  
  /* Set up out-of-process renderer */
  
  /* Create named pipe */
  mkfifo (argv[1], S_IRUSR | S_IWUSR);
  /*mknod (argv[1], S_IRUSR | S_IWUSR | S_IFIFO, 0);*/
  
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

