
#include <clutter/clutter.h>
#include <clutter-mozembed.h>
#include <string.h>
#include <fcntl.h>
#include <math.h>

int
main (int argc, char **argv)
{
  GList *v;
  GDir *dir;
  gchar *text;
  const gchar *filename;
  ClutterActor *stage, *label;
  gint x, y, n, cols, rows, width, height;

  GList *views = NULL;
  GError *error = NULL;

  clutter_init (&argc, &argv);
  
  /* This is a *massive* hack just to demonstrate live previews. Please don't
   * ever do this, use IPC to coordinate doing this cleanly with
   * clutter_mozembed_new_view() and clutter_mozembed_connect_view()
   */
  dir = g_dir_open (g_get_tmp_dir (), 0, &error);
  
  if (!dir)
    {
      g_warning ("Error opening temporary files directory: %s", error->message);
      g_error_free (error);
    }
  else
    {
      while ((filename = g_dir_read_name (dir)))
        {
          gint fd;
          ClutterActor *mozembed;
          gchar *full_file, *input, *output, *command;

          if (!strstr (filename, "clutter-mozheadless-"))
            continue;
          
          full_file = g_build_filename (g_get_tmp_dir (), filename, NULL);

          /* I've *no* idea what happens when two processes are writing on the
           * same pipe in terms of the possibility of interleaved messages...
           */
          
          /* Open the pipe and ask it to create a new view */
          fd = open (full_file, O_WRONLY | O_NDELAY);
          
          if (fd)
            {
              mozembed = clutter_mozembed_new_view ();

              /* Get the pipe names */
              g_object_get (G_OBJECT (mozembed),
                            "input", &input,
                            "output", &output,
                            NULL);

              /* You should NEVER do this, you need to use IPC to tell the
               * parent application to connect the view with
               * clutter_mozembed_connect_view()
               */
              command = g_strdup_printf ("new-view %s %s", input, output);
              write (fd, command, strlen (command) + 1);
              g_free (command);

              close (fd);
              g_free (input);
              g_free (output);

              views = g_list_append (views, mozembed);
            }

          g_free (full_file);
        }
      g_dir_close (dir);
    }

  /* Now put all the views on a stage */
  stage = clutter_stage_get_default ();
  clutter_actor_set_size (stage, 800, 600);

  n = g_list_length (views);
  text = g_strdup_printf ("%d live previews:", n);
  label = clutter_text_new_with_text ("Sans 20px", text);
  
  clutter_container_add_actor (CLUTTER_CONTAINER (stage), label);
  clutter_actor_set_position (label, 10, 10);
  
  x = y = 0;
  rows = MAX (1, (gint)sqrt ((gdouble)n));
  cols = MAX (1, n / rows);
  width = (clutter_actor_get_width (stage) - 20) / cols;
  height = (clutter_actor_get_height (stage) - 60) / rows;
  for (v = views; v; v = v->next)
    {
      ClutterActor *mozembed = CLUTTER_ACTOR (v->data);
      clutter_container_add_actor (CLUTTER_CONTAINER (stage), mozembed);
      clutter_actor_set_position (mozembed, (x*width) + 10, (y*height) + 40);
      clutter_actor_set_size (mozembed, width, height);
      
      x++;
      if (x > cols)
        {
          x = 0;
          y++;
        }
    }
  g_list_free (views);
  
  clutter_actor_show_all (stage);
  
  clutter_main ();
  
  clutter_actor_destroy (stage);
  
  return 0;
}

