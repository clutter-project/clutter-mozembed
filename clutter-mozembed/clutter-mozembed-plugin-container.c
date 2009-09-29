#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "clutter-mozembed-plugin-container.h"

#define CLUTTER_MOZEMBED_PLUGIN_CONTAINER_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), \
   CLUTTER_MOZEMBED_PLUGIN_CONTAINER_TYPE, \
   ClutterMozembedPluginContainerPrivate))

struct _ClutterMozembedPluginContainerPrivate
{
  ClutterActor *stage;
};

/* init methods */
static void clutter_mozembed_plugin_container_class_init (
  ClutterMozembedPluginContainerClass *klass);
static void clutter_mozembed_plugin_container_init (
  ClutterMozembedPluginContainer *container);

/* widget class methods */
static void clutter_mozembed_plugin_container_map (GtkWidget *widget);
static void clutter_mozembed_plugin_container_unmap (GtkWidget *widget);
static void clutter_mozembed_plugin_container_realize (GtkWidget *widget);
static void clutter_mozembed_plugin_container_size_allocate (
  GtkWidget     *widget,
  GtkAllocation *allocation);

/* container class methods */
static void clutter_mozembed_plugin_container_remove (
  GtkContainer *container,
  GtkWidget    *child_widget);
static void clutter_mozembed_plugin_container_forall (
  GtkContainer *container,
  gboolean      include_internals,
  GtkCallback   callback,
  gpointer      callback_data);
static void clutter_mozembed_plugin_container_add (
  GtkContainer *container,
  GtkWidget    *widget);

typedef struct _ClutterMozembedPluginContainerChild \
  ClutterMozembedPluginContainerChild;

struct _ClutterMozembedPluginContainerChild {
  GtkWidget *widget;
  gint x;
  gint y;
};

static void clutter_mozembed_plugin_container_allocate_child (
  ClutterMozembedPluginContainer      *container,
  ClutterMozembedPluginContainerChild *child);
static ClutterMozembedPluginContainerChild *
  clutter_mozembed_plugin_container_get_child (
    ClutterMozembedPluginContainer *container,
    GtkWidget                      *child);

static GtkContainerClass *parent_class = NULL;

/* public methods */

G_DEFINE_TYPE (ClutterMozembedPluginContainer,
               clutter_mozembed_plugin_container,
               GTK_TYPE_CONTAINER)

GtkWidget *
clutter_mozembed_plugin_container_new (void)
{
  ClutterMozembedPluginContainer *container;

  container = g_object_new (CLUTTER_MOZEMBED_PLUGIN_CONTAINER_TYPE, NULL);

  return GTK_WIDGET(container);
}

GtkWidget *
clutter_mozembed_plugin_container_new_for_stage (ClutterActor *stage)
{
  ClutterMozembedPluginContainer *container;
  ClutterMozembedPluginContainerPrivate *priv;

  container = g_object_new (CLUTTER_MOZEMBED_PLUGIN_CONTAINER_TYPE, NULL);

  priv = CLUTTER_MOZEMBED_PLUGIN_CONTAINER (container)->priv;
  priv->stage = stage;

  return GTK_WIDGET(container);
}

void
clutter_mozembed_plugin_container_put (ClutterMozembedPluginContainer *container,
                                       GtkWidget *child_widget,
                                       gint x, gint y)
{
  ClutterMozembedPluginContainerChild *child;

  child = g_new (ClutterMozembedPluginContainerChild, 1);

  child->widget = child_widget;
  child->x = x;
  child->y = y;

  container->children = g_list_append (container->children, child);

  gtk_widget_set_parent (child_widget, GTK_WIDGET (container));
}

void
clutter_mozembed_plugin_container_move (ClutterMozembedPluginContainer *container,
                                        GtkWidget *child_widget,
                                        gint       x,
                                        gint       y,
                                        gint       width,
                                        gint       height)
{
  ClutterMozembedPluginContainerChild *child;
  GtkAllocation new_allocation;

  child = clutter_mozembed_plugin_container_get_child (container, child_widget);

  child->x = x;
  child->y = y;

  new_allocation.x = x;
  new_allocation.y = y;
  new_allocation.width = width;
  new_allocation.height = height;

  gtk_widget_size_allocate (child_widget, &new_allocation);
}

/* static methods */

void
clutter_mozembed_plugin_container_class_init (ClutterMozembedPluginContainerClass *klass)
{
  GtkContainerClass *container_class = GTK_CONTAINER_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  g_type_class_add_private (klass,
                            sizeof (ClutterMozembedPluginContainerPrivate));

  parent_class = g_type_class_peek_parent (klass);

  widget_class->map = clutter_mozembed_plugin_container_map;
  widget_class->unmap = clutter_mozembed_plugin_container_unmap;
  widget_class->realize = clutter_mozembed_plugin_container_realize;
  widget_class->size_allocate = clutter_mozembed_plugin_container_size_allocate;

  container_class->remove = clutter_mozembed_plugin_container_remove;
  container_class->forall = clutter_mozembed_plugin_container_forall;
  container_class->add = clutter_mozembed_plugin_container_add;
}

void
clutter_mozembed_plugin_container_init (ClutterMozembedPluginContainer *container)
{
  ClutterMozembedPluginContainerPrivate *priv;

  priv = container->priv =
    CLUTTER_MOZEMBED_PLUGIN_CONTAINER_GET_PRIVATE (container);

  GTK_WIDGET_UNSET_FLAGS (container, GTK_NO_WINDOW);

  container->container.resize_mode = GTK_RESIZE_IMMEDIATE;
  gtk_widget_set_redraw_on_allocate (GTK_WIDGET (container),
                                     FALSE);

  gtk_widget_set_colormap (GTK_WIDGET (container), gdk_colormap_get_system ());
}

void
clutter_mozembed_plugin_container_map (GtkWidget *widget)
{
  ClutterMozembedPluginContainer *container;
  GList *children;

  g_return_if_fail (CLUTTER_MOZEMBED_IS_PLUGIN_CONTAINER (widget));
  container = CLUTTER_MOZEMBED_PLUGIN_CONTAINER (widget);

  GTK_WIDGET_SET_FLAGS (widget, GTK_MAPPED);

  for (children = container->children; children; children = children->next)
    {
      GtkWidget *child =
        ((ClutterMozembedPluginContainerChild *)children->data)->widget;

      if (GTK_WIDGET_VISIBLE (child) &&
          !GTK_WIDGET_MAPPED (child))
          gtk_widget_map (child);
    }

  gdk_window_show (gtk_widget_get_window (widget));
}

void
clutter_mozembed_plugin_container_unmap (GtkWidget *widget)
{
  ClutterMozembedPluginContainer *container;

  g_return_if_fail (CLUTTER_MOZEMBED_IS_PLUGIN_CONTAINER (widget));
  container = CLUTTER_MOZEMBED_PLUGIN_CONTAINER (widget);

  GTK_WIDGET_UNSET_FLAGS (widget, GTK_MAPPED);

  gdk_window_hide (gtk_widget_get_window (widget));
}

void
clutter_mozembed_plugin_container_realize (GtkWidget *widget)
{
  GdkWindow *window;
  ClutterMozembedPluginContainer *container;

  g_return_if_fail (CLUTTER_MOZEMBED_IS_PLUGIN_CONTAINER (widget));

  container = CLUTTER_MOZEMBED_PLUGIN_CONTAINER(widget);

  GTK_WIDGET_SET_FLAGS(widget, GTK_REALIZED);

#if 0
{
  gint attributes_mask;
  GdkWindowAttr attributes;

  /* On GTK+ >= 2.18, there are default events on the window
   * that you can't override.
   */
  attributes.window_type = GDK_WINDOW_CHILD;
  attributes.x = widget->allocation.x;
  attributes.y = widget->allocation.y;
  attributes.width = widget->allocation.width;
  attributes.height = widget->allocation.height;
  attributes.wclass = GDK_INPUT_OUTPUT;
  attributes.visual = gtk_widget_get_visual (widget);
  attributes.colormap = gtk_widget_get_colormap (widget);
  attributes.event_mask = 0;

  attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;

  window = gdk_window_new (gtk_widget_get_parent_window (widget),
                           &attributes,
                           attributes_mask);
}
#else
{
  Display *xdpy = gdk_x11_get_default_xdisplay ();
  unsigned long pixel = WhitePixel (xdpy, DefaultScreen (xdpy));

  Window container_xwin = XCreateSimpleWindow (xdpy,
                                               GDK_WINDOW_XWINDOW (
                                                 gtk_widget_get_parent_window (
                                                   widget)),
                                               -100, -100,
                                               100, 100,
                                               0, /* border width */
                                               pixel, /* border colour */
                                               pixel); /* bg colour */

  window = gdk_window_foreign_new (container_xwin);
}
#endif

  gtk_widget_set_window (widget, window);
  gdk_window_set_user_data (window, widget);

  gtk_widget_set_style (widget,
                        gtk_style_attach (gtk_widget_get_style (widget),
                                          window));
  gtk_style_set_background (gtk_widget_get_style (widget),
                            window,
                            GTK_STATE_NORMAL);
}

void
clutter_mozembed_plugin_container_size_allocate (GtkWidget     *widget,
                                                 GtkAllocation *allocation)
{
  ClutterMozembedPluginContainer *container;
  GtkAllocation old_allocation;
  GList *tmp_list;

  g_return_if_fail (CLUTTER_MOZEMBED_IS_PLUGIN_CONTAINER (widget));

  /* short circuit if you can */
  container = CLUTTER_MOZEMBED_PLUGIN_CONTAINER (widget);
  gtk_widget_get_allocation (widget, &old_allocation);

  if (!container->children &&
      old_allocation.x == allocation->x &&
      old_allocation.y == allocation->y &&
      old_allocation.width == allocation->width &&
      old_allocation.height == allocation->height)
    return;

  gtk_widget_set_allocation (widget, allocation);

  tmp_list = container->children;

  for (tmp_list = container->children; tmp_list; tmp_list = tmp_list->next)
    {
      ClutterMozembedPluginContainerChild *child = tmp_list->data;
      clutter_mozembed_plugin_container_allocate_child (container, child);
    }

  if (GTK_WIDGET_REALIZED (widget))
    {
      GdkWindow *window = gtk_widget_get_window (widget);
      gdk_window_move_resize (window,
                              allocation->x,
                              allocation->y,
                              allocation->width,
                              allocation->height);
    }
}

void
clutter_mozembed_plugin_container_remove (GtkContainer *container,
                                          GtkWidget    *child_widget)
{
  ClutterMozembedPluginContainerChild *child;
  ClutterMozembedPluginContainer *clutter_mozembed_plugin_container;

  g_return_if_fail (CLUTTER_MOZEMBED_IS_PLUGIN_CONTAINER (container));
  g_return_if_fail (GTK_IS_WIDGET (child_widget));

  clutter_mozembed_plugin_container =
    CLUTTER_MOZEMBED_PLUGIN_CONTAINER (container);

  child =
    clutter_mozembed_plugin_container_get_child (clutter_mozembed_plugin_container,
                                                 child_widget);
  g_return_if_fail (child);

  gtk_widget_unparent (child_widget);

  clutter_mozembed_plugin_container->children =
    g_list_remove (clutter_mozembed_plugin_container->children, child);
  g_free (child);
}

void
clutter_mozembed_plugin_container_forall (GtkContainer *container,
                                          gboolean      include_internals,
                                          GtkCallback   callback,
                                          gpointer      callback_data)
{
  ClutterMozembedPluginContainer *clutter_mozembed_plugin_container;
  GList *tmp_list;

  g_return_if_fail (CLUTTER_MOZEMBED_IS_PLUGIN_CONTAINER (container));
  g_return_if_fail (callback != NULL);

  clutter_mozembed_plugin_container =
    CLUTTER_MOZEMBED_PLUGIN_CONTAINER (container);

  tmp_list = clutter_mozembed_plugin_container->children;
  while (tmp_list)
    {
      ClutterMozembedPluginContainerChild *child = tmp_list->data;
      tmp_list = tmp_list->next;
      (* callback) (child->widget, callback_data);
    }
}

static void
clutter_mozembed_plugin_container_allocate_child (ClutterMozembedPluginContainer *container,
                                                  ClutterMozembedPluginContainerChild *child)
{
  GtkAllocation allocation;

  gtk_widget_get_allocation (child->widget, &allocation);
  allocation.x = child->x;
  allocation.y = child->y;

  gtk_widget_size_allocate (child->widget, &allocation);
}

ClutterMozembedPluginContainerChild *
clutter_mozembed_plugin_container_get_child (ClutterMozembedPluginContainer *container,
                                             GtkWidget *child_widget)
{
  GList *c;

  for (c = container->children; c; c = c->next)
    {
      ClutterMozembedPluginContainerChild *child = c->data;
      if (child->widget == child_widget)
        return child;
    }

  return NULL;
}

static void 
clutter_mozembed_plugin_container_add (GtkContainer *container,
                                       GtkWidget    *widget)
{
  clutter_mozembed_plugin_container_put (CLUTTER_MOZEMBED_PLUGIN_CONTAINER (container),
                                         widget,
                                         0, 0);
}

