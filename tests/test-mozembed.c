
#include <clutter/clutter.h>
#include "clutter-mozembed.h"

int
main (int argc, char **argv)
{
  ClutterTimeline *timeline1, *timeline2;
  ClutterBehaviour *x_rot, *y_rot;
  ClutterActor *stage, *mozembed;
  
  clutter_init (&argc, &argv);
  
  clutter_set_motion_events_frequency (60);
  
  stage = clutter_stage_get_default ();
  clutter_actor_set_size (stage, 800, 600);
  
  mozembed = clutter_mozembed_new ();
  clutter_container_add_actor (CLUTTER_CONTAINER (stage), mozembed);
  clutter_actor_set_size (mozembed, 780, 580);
  clutter_actor_set_position (mozembed, 10, 10);
  clutter_actor_move_anchor_point (mozembed, 390, 290);
  
  timeline1 = clutter_timeline_new_for_duration (1000);
  timeline2 = clutter_timeline_new_for_duration (1500);
  x_rot =
    clutter_behaviour_rotate_new (clutter_alpha_new_full (timeline1,
                                                          CLUTTER_ALPHA_SINE,
                                                          NULL,
                                                          NULL),
                                  CLUTTER_X_AXIS,
                                  CLUTTER_ROTATE_CW,
                                  0,
                                  10);
  y_rot =
    clutter_behaviour_rotate_new (clutter_alpha_new_full (timeline2,
                                                          CLUTTER_ALPHA_SINE,
                                                          NULL,
                                                          NULL),
                                  CLUTTER_Y_AXIS,
                                  CLUTTER_ROTATE_CCW,
                                  10,
                                  0);
  clutter_behaviour_apply (x_rot, mozembed);
  clutter_behaviour_apply (y_rot, mozembed);
  clutter_timeline_set_loop (timeline1, TRUE);
  clutter_timeline_set_loop (timeline2, TRUE);
  //clutter_timeline_start (timeline1);
  //clutter_timeline_start (timeline2);
  
  if (argc == 2)
    clutter_mozembed_open (CLUTTER_MOZEMBED (mozembed), argv[1]);
  else
    clutter_mozembed_open (CLUTTER_MOZEMBED (mozembed),
                           "http://news.google.co.uk/");
  
  clutter_actor_show (stage);

  clutter_stage_set_key_focus (CLUTTER_STAGE (stage), mozembed);
  
  clutter_main ();
  
  return 0;
}

