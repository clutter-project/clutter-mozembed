#ifndef __CLUTTER_MOZEMBED_PLUGIN_CONTAINER_H__
#define __CLUTTER_MOZEMBED_PLUGIN_CONTAINER_H__

#include <gtk/gtk.h>
#include <clutter/clutter.h>

G_BEGIN_DECLS

#define CLUTTER_MOZEMBED_PLUGIN_CONTAINER_TYPE \
  (clutter_mozembed_plugin_container_get_type())
#define CLUTTER_MOZEMBED_PLUGIN_CONTAINER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                               CLUTTER_MOZEMBED_PLUGIN_CONTAINER_TYPE, \
                               ClutterMozembedPluginContainer))
#define CLUTTER_MOZEMBED_PLUGIN_CONTAINER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
                            CLUTTER_MOZEMBED_PLUGIN_CONTAINER_TYPE, \
                            ClutterMozembedPluginContainerClass))
#define CLUTTER_MOZEMBED_IS_PLUGIN_CONTAINER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                               CLUTTER_MOZEMBED_PLUGIN_CONTAINER_TYPE))
#define CLUTTER_MOZEMBED_IS_PLUGIN_CONTAINER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
                            CLUTTER_MOZEMBED_PLUGIN_CONTAINER_TYPE))
#define CLUTTER_MOZEMBED_PLUGIN_CONAINTER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
                              CLUTTER_MOZEMBED_PLUGIN_CONTAINER_TYPE, \
                              ClutterMozembedPluginContainerClass))

typedef struct _ClutterMozembedPluginContainer ClutterMozembedPluginContainer;
typedef struct _ClutterMozembedPluginContainerClass ClutterMozembedPluginContainerClass;
typedef struct _ClutterMozembedPluginContainerPrivate ClutterMozembedPluginContainerPrivate;

struct _ClutterMozembedPluginContainer
{
  GtkContainer   container;
  GList         *children;
  ClutterMozembedPluginContainerPrivate *priv;
};

struct _ClutterMozembedPluginContainerClass
{
  GtkContainerClass parent_class;
};

GType      clutter_mozembed_plugin_container_get_type      (void);
GtkWidget *clutter_mozembed_plugin_container_new           (void);
GtkWidget *clutter_mozembed_plugin_container_new_for_stage (ClutterActor *stage);

void clutter_mozembed_plugin_container_put
  (ClutterMozembedPluginContainer *container,
   GtkWidget    *child_widget,
   gint          x,
   gint          y);

void clutter_mozembed_plugin_container_move
  (ClutterMozembedPluginContainer *container,
   GtkWidget    *child_widget,
   gint          x,
   gint          y,
   gint          width,
   gint          height);

G_END_DECLS

#endif /* __CLUTTER_MOZEMBED_PLUGIN_CONTAINER_H__ */
