#ifndef _CLUTTER_MOZEMBED
#define _CLUTTER_MOZEMBED

#include <glib-object.h>
#include <clutter/clutter.h>

G_BEGIN_DECLS

#define CLUTTER_TYPE_MOZEMBED clutter_mozembed_get_type()

#define CLUTTER_MOZEMBED(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  CLUTTER_TYPE_MOZEMBED, ClutterMozEmbed))

#define CLUTTER_MOZEMBED_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  CLUTTER_TYPE_MOZEMBED, ClutterMozEmbedClass))

#define CLUTTER_IS_MOZEMBED(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  CLUTTER_TYPE_MOZEMBED))

#define CLUTTER_IS_MOZEMBED_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  CLUTTER_TYPE_MOZEMBED))

#define CLUTTER_MOZEMBED_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  CLUTTER_TYPE_MOZEMBED, ClutterMozEmbedClass))

typedef struct _ClutterMozEmbedPrivate ClutterMozEmbedPrivate;

typedef struct {
  ClutterActor parent;
  
  ClutterMozEmbedPrivate *priv;
} ClutterMozEmbed;

typedef struct {
  ClutterActorClass parent_class;
  
  /* Signals */
  void (* progress)  (ClutterMozEmbed *mozembed, gdouble progress);
  void (* net_start) (ClutterMozEmbed *mozembed);
  void (* net_stop)  (ClutterMozEmbed *mozembed);
} ClutterMozEmbedClass;

GType clutter_mozembed_get_type (void);

ClutterActor* clutter_mozembed_new (void);

void clutter_mozembed_open (ClutterMozEmbed *mozembed, const gchar *uri);
const gchar *clutter_mozembed_get_location (ClutterMozEmbed *mozembed);
const gchar *clutter_mozembed_get_title (ClutterMozEmbed *mozembed);

G_END_DECLS

#endif /* _CLUTTER_MOZEMBED */

