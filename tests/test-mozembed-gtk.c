
#include <clutter/clutter.h>
#include <clutter-mozembed/clutter-mozembed.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <clutter/x11/clutter-x11.h>
#include <clutter-gtk/clutter-gtk.h>

static void
progress_cb (ClutterMozEmbedDownload *download,
             GParamSpec              *pspec,
             ClutterActor            *rect)
{
  gdouble progress =
    clutter_mozembed_download_get_complete (download) ?
      1.0 :
      (clutter_mozembed_download_get_progress (download) /
        (gdouble)clutter_mozembed_download_get_max_progress (download));
  clutter_actor_set_width (rect, 280 * progress);
}

static void
download_cb (ClutterMozEmbed         *mozembed,
             ClutterMozEmbedDownload *download)
{
  gchar *text;
  ClutterActor *stage, *label1, *label2, *rect1, *rect2;
  const ClutterColor bar_fg = { 0x00, 0x00, 0xc0, 0xff };
  const ClutterColor bar_bg = { 0xc0, 0xc0, 0xc0, 0xff };

  stage = clutter_stage_new ();
  clutter_actor_set_size (stage, 320, 200);

  text = g_strdup_printf ("Source: %s",
                          clutter_mozembed_download_get_source (download));
  label1 = clutter_text_new_with_text ("Sans 20px", text);
  clutter_text_set_ellipsize (CLUTTER_TEXT (label1), PANGO_ELLIPSIZE_MIDDLE);
  clutter_actor_set_width (label1, 280);
  g_free (text);

  text = g_strdup_printf ("Destination: %s",
                          clutter_mozembed_download_get_destination (download));
  label2 = clutter_text_new_with_text ("Sans 20px", text);
  clutter_text_set_ellipsize (CLUTTER_TEXT (label2), PANGO_ELLIPSIZE_MIDDLE);
  clutter_actor_set_width (label2, 280);
  g_free (text);

  rect2 = clutter_rectangle_new_with_color (&bar_bg);
  clutter_actor_set_size (rect2, 280, 80);

  rect1 = clutter_rectangle_new_with_color (&bar_fg);
  clutter_actor_set_size (rect1, 1, 80);

  clutter_container_add (CLUTTER_CONTAINER (stage),
                         label1, label2, rect2, rect1, NULL);

  clutter_actor_set_position (label1, 20, 20);
  clutter_actor_set_position (label2, 20, 60);
  clutter_actor_set_position (rect2, 20, 100);
  clutter_actor_set_position (rect1, 20, 100);

  g_signal_connect (download, "notify::progress",
                    G_CALLBACK (progress_cb), rect1);
  g_signal_connect (download, "notify::complete",
                    G_CALLBACK (progress_cb), rect1);

  clutter_actor_show_all (stage);
}

static void
switch_direction_cb (ClutterTimeline *timeline)
{
  clutter_timeline_set_direction (timeline,
                                  1-clutter_timeline_get_direction (timeline));
}

static void
size_request_cb (ClutterMozEmbed *mozembed,
                 gint             width,
                 gint             height,
                 ClutterActor    *stage)
{
  clutter_actor_set_size (CLUTTER_ACTOR (mozembed), width, height);
  clutter_actor_set_size (stage, width, height);
}

static void
close_cb (ClutterMozEmbed *mozembed,
          ClutterActor    *stage)
{
  clutter_actor_destroy (stage);
}

static void
new_window_cb (ClutterMozEmbed  *mozembed,
               ClutterActor    **new_mozembed,
               guint             chromeflags)
{
  ClutterTimeline *timeline;
  ClutterBehaviour *z_rot;
  ClutterActor *stage;

  /* FIXME: Use Gtk here - see below */
  stage = clutter_stage_new ();

  *new_mozembed = clutter_mozembed_new_for_new_window ();

  clutter_actor_set_size (stage, 640, 480);
  clutter_container_add_actor (CLUTTER_CONTAINER (stage), *new_mozembed);
  clutter_actor_set_size (*new_mozembed, 640, 480);
  g_signal_connect (*new_mozembed, "size-request",
                    G_CALLBACK (size_request_cb), stage);
  g_signal_connect (*new_mozembed, "closed",
                    G_CALLBACK (close_cb), stage);
  g_signal_connect (*new_mozembed, "download",
                    G_CALLBACK (download_cb), NULL);

  timeline = clutter_timeline_new (8000);
  z_rot =
    clutter_behaviour_rotate_new (clutter_alpha_new_full (
                                    timeline, CLUTTER_LINEAR),
                                  CLUTTER_Z_AXIS,
                                  CLUTTER_ROTATE_CW,
                                  0,
                                  360);
  clutter_behaviour_rotate_set_center (CLUTTER_BEHAVIOUR_ROTATE (z_rot),
                                       320, 240, 0);
  clutter_behaviour_apply (z_rot, *new_mozembed);
  clutter_timeline_set_loop (timeline, TRUE);
  //clutter_timeline_start (timeline);

  clutter_actor_show (stage);
  clutter_stage_set_key_focus (CLUTTER_STAGE (stage), *new_mozembed);
}

static GtkWidget *layout, *clutter_embed;

static void
on_layout_size_allocated (GtkWidget     *widget,
                          GtkAllocation *allocation,
                          gpointer       user_data)
{
  gtk_widget_size_allocate (GTK_WIDGET (user_data), allocation);
}

static GdkFilterReturn
gdk_to_clutter_event_forward (GdkXEvent *xevent,
                              GdkEvent  *event,
                              gpointer   data)
{
  if (((XEvent *)xevent)->type == ButtonPress)
    gtk_widget_grab_focus (clutter_embed);

  if (((XAnyEvent*)xevent)->window ==
      GDK_WINDOW_XWINDOW (gtk_widget_get_window (layout)))
    {
      /* Forward the events on layout to GtkClutterEmbed */
      ((XAnyEvent*)xevent)->window =
        GDK_WINDOW_XWINDOW (gtk_widget_get_window (clutter_embed));
    }

  switch (clutter_x11_handle_event ((XEvent*)xevent))
    {
    default:
    case CLUTTER_X11_FILTER_CONTINUE:
      return GDK_FILTER_CONTINUE;
    case CLUTTER_X11_FILTER_TRANSLATE:
      return GDK_FILTER_TRANSLATE;
    case CLUTTER_X11_FILTER_REMOVE:
      return GDK_FILTER_REMOVE;
    }

  return GDK_FILTER_CONTINUE;
}

int
main (int argc, char **argv)
{
  ClutterTimeline *timeline1, *timeline2;
  ClutterBehaviour *x_rot, *y_rot;
  ClutterActor *stage, *mozembed;
  GtkWidget *window;
  gint event_mask;

  if (!gtk_clutter_init (&argc, &argv))
    g_error ("Error initialising GtkClutter\n");

  gdk_window_add_filter (NULL, gdk_to_clutter_event_forward, NULL);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_default_size (GTK_WINDOW (window), 800, 600);
  g_signal_connect (window, "destroy", G_CALLBACK (gtk_main_quit), NULL);

  layout = gtk_fixed_new ();
  gtk_fixed_set_has_window (GTK_FIXED (layout), TRUE);
  event_mask = GDK_BUTTON_PRESS_MASK   |
               GDK_BUTTON_RELEASE_MASK |
               GDK_KEY_PRESS_MASK      |
               GDK_KEY_RELEASE_MASK    |
               GDK_POINTER_MOTION_MASK;
  gtk_widget_add_events (layout, event_mask);

  clutter_embed = gtk_clutter_embed_new ();

  gtk_container_add (GTK_CONTAINER (layout), clutter_embed);
  gtk_container_add (GTK_CONTAINER (window), GTK_WIDGET (layout));

  g_signal_connect (layout, "size-allocate",
                    G_CALLBACK (on_layout_size_allocated), clutter_embed);

  gtk_widget_show (clutter_embed);

  stage = gtk_clutter_embed_get_stage (GTK_CLUTTER_EMBED (clutter_embed));

  mozembed = clutter_mozembed_new ();
  clutter_container_add_actor (CLUTTER_CONTAINER (stage), mozembed);
  clutter_actor_set_size (mozembed, 780, 580);
  clutter_actor_set_position (mozembed, 10, 10);
  clutter_actor_move_anchor_point (mozembed, 390, 290);

  timeline1 = clutter_timeline_new (1000);
  timeline2 = clutter_timeline_new (1500);
  x_rot =
    clutter_behaviour_rotate_new (clutter_alpha_new_full (
                                    timeline1, CLUTTER_EASE_IN_OUT_SINE),
                                  CLUTTER_X_AXIS,
                                  CLUTTER_ROTATE_CW,
                                  0,
                                  10);
  y_rot =
    clutter_behaviour_rotate_new (clutter_alpha_new_full (
                                    timeline2, CLUTTER_EASE_IN_OUT_SINE),
                                  CLUTTER_Y_AXIS,
                                  CLUTTER_ROTATE_CCW,
                                  10,
                                  0);
  //clutter_behaviour_apply (x_rot, mozembed);
  //clutter_behaviour_apply (y_rot, mozembed);
  clutter_timeline_set_loop (timeline1, TRUE);
  clutter_timeline_set_loop (timeline2, TRUE);
  g_signal_connect (timeline1, "completed",
                    G_CALLBACK (switch_direction_cb), NULL);
  g_signal_connect (timeline2, "completed",
                    G_CALLBACK (switch_direction_cb), NULL);
  clutter_timeline_start (timeline1);
  clutter_timeline_start (timeline2);

  g_signal_connect (mozembed, "new-window",
                    G_CALLBACK (new_window_cb), NULL);
  g_signal_connect (mozembed, "download",
                    G_CALLBACK (download_cb), NULL);

  if (argc == 2)
    clutter_mozembed_open (CLUTTER_MOZEMBED (mozembed), argv[1]);
  else
    clutter_mozembed_open (CLUTTER_MOZEMBED (mozembed),
                           "http://news.google.co.uk/");

  clutter_stage_set_key_focus (CLUTTER_STAGE (stage), mozembed);

  clutter_mozembed_set_layout_container (CLUTTER_MOZEMBED (mozembed),
                                         layout);
  gtk_widget_show_all (window);
  gtk_main ();

  return 0;
}

