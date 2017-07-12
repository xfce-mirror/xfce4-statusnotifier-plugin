/*
 *  Copyright (c) 2017 Viktor Odintsev <ninetls@xfce.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */



#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <libxfce4panel/libxfce4panel.h>

#include "sn-icon-box.h"



static void                  sn_icon_box_finalize                    (GObject                 *object);

static void                  sn_icon_box_icon_changed                (GtkWidget               *widget);

static void                  sn_icon_box_get_preferred_width         (GtkWidget               *widget,
                                                                      gint                    *minimum_width,
                                                                      gint                    *natural_width);

static void                  sn_icon_box_get_preferred_height        (GtkWidget               *widget,
                                                                      gint                    *minimum_height,
                                                                      gint                    *natural_height);

static void                  sn_icon_box_size_allocate               (GtkWidget               *widget,
                                                                      GtkAllocation           *allocation);

static void                  sn_icon_box_remove                      (GtkContainer            *container,
                                                                      GtkWidget               *child);

static GType                 sn_icon_box_child_type                  (GtkContainer            *container);

static void                  sn_icon_box_forall                      (GtkContainer            *container,
                                                                      gboolean                 include_internals,
                                                                      GtkCallback              callback,
                                                                      gpointer                 callback_data);


struct _SnIconBoxClass
{
  GtkContainerClass    __parent__;
};

struct _SnIconBox
{
  GtkContainer         __parent__;

  SnItem              *item;
  SnConfig            *config;

  GtkWidget           *icon;
  GtkWidget           *overlay;

  guint                item_icon_changed_handler;
  guint                config_notify_icon_size_handler;
};

G_DEFINE_TYPE (SnIconBox, sn_icon_box, GTK_TYPE_CONTAINER)



static void
sn_icon_box_class_init (SnIconBoxClass *klass)
{
  GObjectClass      *object_class;
  GtkWidgetClass    *widget_class;
  GtkContainerClass *container_class;

  object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = sn_icon_box_finalize;

  widget_class = GTK_WIDGET_CLASS (klass);
  widget_class->get_preferred_width = sn_icon_box_get_preferred_width;
  widget_class->get_preferred_height = sn_icon_box_get_preferred_height;
  widget_class->size_allocate = sn_icon_box_size_allocate;

  container_class = GTK_CONTAINER_CLASS (klass);
  container_class->remove = sn_icon_box_remove;
  container_class->child_type = sn_icon_box_child_type;
  container_class->forall = sn_icon_box_forall;
}



static void
sn_icon_box_init (SnIconBox *box)
{
  gtk_widget_set_has_window (GTK_WIDGET (box), FALSE);
  gtk_widget_set_can_focus (GTK_WIDGET (box), FALSE);
  gtk_widget_set_can_default (GTK_WIDGET (box), FALSE);
  gtk_container_set_border_width (GTK_CONTAINER (box), 0);
  gtk_widget_set_name (GTK_WIDGET (box), "sn-button-box");

  box->item = NULL;
  box->config = NULL;

  box->icon = NULL;
  box->overlay = NULL;

  box->item_icon_changed_handler = 0;
  box->config_notify_icon_size_handler = 0;
}



static GType
sn_icon_box_child_type (GtkContainer *container)
{
  return GTK_TYPE_WIDGET;
}



static void
sn_icon_box_forall (GtkContainer *container,
                    gboolean      include_internals,
                    GtkCallback   callback,
                    gpointer      callback_data)
{
  SnIconBox *box = XFCE_SN_ICON_BOX (container);

  /* z-order depends on forall order */

  if (box->overlay != NULL)
    (*callback) (box->overlay, callback_data);

  if (box->icon != NULL)
    (*callback) (box->icon, callback_data);
}



static void
sn_icon_box_remove (GtkContainer *container,
                    GtkWidget    *child)
{
  SnIconBox *box;

  g_return_if_fail (XFCE_IS_SN_ICON_BOX (container));

  box = XFCE_SN_ICON_BOX (container);

  if (child == box->icon)
    {
      gtk_widget_unparent (child);
      box->icon = NULL;
    }
  else if (child == box->overlay)
    {
      gtk_widget_unparent (child);
      box->overlay = NULL;
    }

  gtk_widget_queue_resize (GTK_WIDGET (container));
}



GtkWidget *
sn_icon_box_new (SnItem   *item,
                 SnConfig *config)
{
  SnIconBox *box = g_object_new (XFCE_TYPE_SN_ICON_BOX, NULL);

  g_return_val_if_fail (XFCE_IS_SN_CONFIG (config), NULL);

  box->item = item;
  box->config = config;

  box->icon = gtk_image_new ();
  gtk_widget_set_parent (box->icon, GTK_WIDGET (box));
  gtk_widget_show (box->icon);

  box->overlay = gtk_image_new ();
  gtk_widget_set_parent (box->overlay, GTK_WIDGET (box));
  gtk_widget_show (box->overlay);

  box->config_notify_icon_size_handler = 
    g_signal_connect_swapped (config, "notify::icon-size",
                              G_CALLBACK (sn_icon_box_icon_changed), box);
  box->item_icon_changed_handler = 
    g_signal_connect_swapped (item, "icon-changed",
                              G_CALLBACK (sn_icon_box_icon_changed), box);
  sn_icon_box_icon_changed (GTK_WIDGET (box));

  return GTK_WIDGET (box);
}



static void
sn_icon_box_finalize (GObject *object)
{
  SnIconBox *box = XFCE_SN_ICON_BOX (object);

  if (box->item_icon_changed_handler != 0)
    g_signal_handler_disconnect (box->item, box->item_icon_changed_handler);
  if (box->config_notify_icon_size_handler != 0)
    g_signal_handler_disconnect (box->config, box->config_notify_icon_size_handler);

  G_OBJECT_CLASS (sn_icon_box_parent_class)->finalize (object);
}



static void
sn_icon_box_apply_icon (GtkWidget    *image,
                        GtkIconTheme *icon_theme,
                        const gchar  *icon_name,
                        GdkPixbuf    *icon_pixbuf,
                        gint          icon_size)
{
  GtkIconInfo *icon_info;
  gboolean     use_pixbuf = TRUE;
  gint         width, height;

  gtk_image_clear (GTK_IMAGE (image));

  if (icon_name != NULL)
    {
      icon_info = gtk_icon_theme_lookup_icon (icon_theme, icon_name, 16, 0);
      if (icon_info != NULL)
        {
          gtk_image_set_from_icon_name (GTK_IMAGE (image), icon_name, GTK_ICON_SIZE_BUTTON);
          g_object_unref (icon_info);
          use_pixbuf = FALSE;
        }
    }

  if (use_pixbuf && icon_pixbuf != NULL)
    {
      width = gdk_pixbuf_get_width (icon_pixbuf);
      height = gdk_pixbuf_get_height (icon_pixbuf);
      if (width > icon_size || height > icon_size)
        {
          /* scale pixbuf */
          if (width > height)
            {
              height = icon_size * height / width;
              width = icon_size;
            }
          else
            {
              width = icon_size * width / height;
              height = icon_size;
            }
          icon_pixbuf = gdk_pixbuf_scale_simple (icon_pixbuf,
                                                 width, height, GDK_INTERP_BILINEAR);
          gtk_image_set_from_pixbuf (GTK_IMAGE (image), icon_pixbuf);
          g_object_unref (icon_pixbuf);
        }
      else
        {
          gtk_image_set_from_pixbuf (GTK_IMAGE (image), icon_pixbuf);
        }
    }

  gtk_image_set_pixel_size (GTK_IMAGE (image), icon_size);
}



static void
sn_icon_box_icon_changed (GtkWidget *widget)
{
  SnIconBox    *box;
  const gchar  *icon_name;
  GdkPixbuf    *icon_pixbuf;
  const gchar  *overlay_icon_name;
  GdkPixbuf    *overlay_icon_pixbuf;
  GtkIconTheme *icon_theme;
  gint          icon_size;

  box = XFCE_SN_ICON_BOX (widget);
  icon_theme = gtk_icon_theme_get_for_screen (gtk_widget_get_screen (GTK_WIDGET (widget)));
  icon_size = sn_config_get_icon_size (box->config);

  sn_item_get_icon (box->item,
                    &icon_name, &icon_pixbuf,
                    &overlay_icon_name, &overlay_icon_pixbuf);

  sn_icon_box_apply_icon (box->icon, icon_theme,
                          icon_name, icon_pixbuf, icon_size);
  sn_icon_box_apply_icon (box->overlay, icon_theme,
                          overlay_icon_name, overlay_icon_pixbuf, icon_size);
}



static void
sn_icon_box_get_preferred_size (GtkWidget *widget,
                                gint      *minimum_size,
                                gint      *natural_size)
{
  SnIconBox      *box = XFCE_SN_ICON_BOX (widget);
  gint            icon_size;
  GtkRequisition  child_req;

  icon_size = sn_config_get_icon_size (box->config);

  if (box->icon != NULL)
    gtk_widget_get_preferred_size (box->icon, NULL, &child_req);

  if (box->overlay != NULL)
    gtk_widget_get_preferred_size (box->overlay, NULL, &child_req);

  if (minimum_size != NULL)
    *minimum_size = icon_size;

  if (natural_size != NULL)
    *natural_size = icon_size;
}



static void
sn_icon_box_get_preferred_width (GtkWidget *widget,
                                 gint      *minimum_width,
                                 gint      *natural_width)
{
  sn_icon_box_get_preferred_size (widget, minimum_width, natural_width);
}



static void
sn_icon_box_get_preferred_height (GtkWidget *widget,
                                  gint      *minimum_height,
                                  gint      *natural_height)
{
  sn_icon_box_get_preferred_size (widget, minimum_height, natural_height);
}



static void
sn_icon_box_size_allocate (GtkWidget     *widget,
                           GtkAllocation *allocation)
{
  SnIconBox *box = XFCE_SN_ICON_BOX (widget);

  gtk_widget_set_allocation (widget, allocation);

  if (box->icon != NULL)
    gtk_widget_size_allocate (box->icon, allocation);

  if (box->overlay != NULL)
    gtk_widget_size_allocate (box->overlay, allocation);
}
