
#include <clutter/clutter.h>
#include <clutter-mozembed.h>
#include <math.h>

int
main (int argc, char **argv)
{
  gchar *text;
  GList *p, *previews;
  ClutterActor *stage, *label;
  gint x, y, n, cols, rows, width, height;
  
  clutter_init (&argc, &argv);
  
  stage = clutter_stage_get_default ();
  clutter_actor_set_size (stage, 800, 600);

  previews = clutter_mozembed_get_live_previews ();
  
  n = g_list_length (previews);
  text = g_strdup_printf ("%d live previews:", n);
  label = clutter_text_new_with_text ("Sans 20px", text);
  
  clutter_container_add_actor (CLUTTER_CONTAINER (stage), label);
  clutter_actor_set_position (label, 10, 10);
  
  x = y = 0;
  rows = MAX (1, (gint)sqrt ((gdouble)n));
  cols = MAX (1, n / rows);
  width = (clutter_actor_get_width (stage) - 20) / cols;
  height = (clutter_actor_get_height (stage) - 60) / rows;
  for (p = previews; p; p = p->next)
    {
      ClutterActor *mozembed = CLUTTER_ACTOR (p->data);
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
  g_list_free (previews);
  
  clutter_actor_show_all (stage);
  
  clutter_main ();
  
  clutter_actor_destroy (stage);
  
  return 0;
}

