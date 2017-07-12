/*
 *  Copyright (c) 2012-2013 Andrzej Radecki <andrzejr@xfce.org>
 *  Copyright (c) 2017      Viktor Odintsev <ninetls@xfce.org>
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

#include "sn-box.h"
#include "sn-button.h"



static void                  sn_box_finalize                         (GObject                 *object);

static void                  sn_box_collect_known_items              (SnBox                   *box,
                                                                      GHashTable              *result);

static void                  sn_box_list_changed                     (SnBox                   *box,
                                                                      SnConfig                *config);

static void                  sn_box_add                              (GtkContainer            *container,
                                                                      GtkWidget               *child);

static void                  sn_box_remove                           (GtkContainer            *container,
                                                                      GtkWidget               *child);

static void                  sn_box_forall                           (GtkContainer            *container,
                                                                      gboolean                 include_internals,
                                                                      GtkCallback              callback,
                                                                      gpointer                 callback_data);

static GType                 sn_box_child_type                       (GtkContainer            *container);

static void                  sn_box_get_preferred_length             (GtkWidget               *widget,
                                                                      gint                    *minimal_length,
                                                                      gint                    *natural_length);

static void                  sn_box_get_preferred_width              (GtkWidget               *widget,
                                                                      gint                    *minimal_width,
                                                                      gint                    *natural_width);

static void                  sn_box_get_preferred_height             (GtkWidget               *widget,
                                                                      gint                    *minimal_height,
                                                                      gint                    *natural_height);

static void                  sn_box_size_allocate                    (GtkWidget               *widget,
                                                                     GtkAllocation            *allocation);



struct _SnBoxClass
{
  GtkContainerClass    __parent__;
};

struct _SnBox
{
  GtkContainer         __parent__;

  SnConfig            *config;

  /* in theory it's possible to have multiple items with same name */
  GHashTable          *children;

  gulong               config_collect_known_items_handler;
  gulong               config_items_list_changed_handler;
};

G_DEFINE_TYPE (SnBox, sn_box, GTK_TYPE_CONTAINER)



static void
sn_box_class_init (SnBoxClass *klass)
{
  GObjectClass      *object_class;
  GtkWidgetClass    *widget_class;
  GtkContainerClass *container_class;

  object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = sn_box_finalize;

  widget_class = GTK_WIDGET_CLASS (klass);
  widget_class->get_preferred_width = sn_box_get_preferred_width;
  widget_class->get_preferred_height = sn_box_get_preferred_height;
  widget_class->size_allocate = sn_box_size_allocate;

  container_class = GTK_CONTAINER_CLASS (klass);
  container_class->add = sn_box_add;
  container_class->remove = sn_box_remove;
  container_class->forall = sn_box_forall;
  container_class->child_type = sn_box_child_type;
}



static void
sn_box_init (SnBox *box)
{
  gtk_widget_set_has_window (GTK_WIDGET (box), FALSE);
  gtk_widget_set_can_focus (GTK_WIDGET (box), TRUE);
  gtk_container_set_border_width (GTK_CONTAINER (box), 0);

  box->children = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}



static void
sn_box_finalize (GObject *object)
{
  SnBox *box = XFCE_SN_BOX (object);

  if (box->config_collect_known_items_handler != 0)
    {
      g_signal_handler_disconnect (box->config, box->config_collect_known_items_handler);
      box->config_collect_known_items_handler = 0;
    }

  if (box->config_items_list_changed_handler != 0)
    {
      g_signal_handler_disconnect (box->config, box->config_items_list_changed_handler);
      box->config_items_list_changed_handler = 0;
    }

  g_hash_table_destroy (box->children);

  G_OBJECT_CLASS (sn_box_parent_class)->finalize (object);
}



GtkWidget *
sn_box_new (SnConfig *config)
{
  SnBox *box = g_object_new (XFCE_TYPE_SN_BOX, NULL);

  box->config = config;

  box->config_collect_known_items_handler =
    g_signal_connect_swapped (G_OBJECT (box->config), "collect-known-items",
                              G_CALLBACK (sn_box_collect_known_items), box);
  box->config_items_list_changed_handler =
    g_signal_connect_swapped (G_OBJECT (box->config), "items-list-changed",
                              G_CALLBACK (sn_box_list_changed), box);

  return GTK_WIDGET (box);
}



static void
sn_box_collect_known_items_callback (GtkWidget *widget,
                                     gpointer   user_data)
{
  SnButton   *button = XFCE_SN_BUTTON (widget);
  GHashTable *table = user_data;
  gchar      *name;

  name = g_strdup (sn_button_get_name (button));
  g_hash_table_replace (table, name, name);
}



static void
sn_box_collect_known_items (SnBox      *box,
                            GHashTable *result)
{
  gtk_container_foreach (GTK_CONTAINER (box),
                         sn_box_collect_known_items_callback, result);
}



static void
sn_box_list_changed (SnBox    *box,
                     SnConfig *config)
{
  SnButton *button;
  GList    *known_items, *li, *li_int, *li_tmp;

  g_return_if_fail (XFCE_IS_SN_BOX (box));
  g_return_if_fail (XFCE_IS_SN_CONFIG (config));

  gtk_container_foreach (GTK_CONTAINER (box), (GtkCallback)gtk_widget_unmap, NULL);

  known_items = sn_config_get_known_items (box->config);
  for (li = known_items; li != NULL; li = li->next)
    {
      li_int = g_hash_table_lookup (box->children, li->data);
      for (li_tmp = li_int; li_tmp != NULL; li_tmp = li_tmp->next)
        {
          button = li_tmp->data;
          if (!sn_config_is_hidden (box->config,
                                    sn_button_get_name (button)))
            {
              gtk_widget_map (GTK_WIDGET (button));
            }
        }
    }

  gtk_widget_queue_resize (GTK_WIDGET (box));
}



static void
sn_box_add (GtkContainer *container,
            GtkWidget    *child)
{
  SnBox       *box = XFCE_SN_BOX (container);
  SnButton    *button = XFCE_SN_BUTTON (child);
  GList       *li;
  const gchar *name;

  g_return_if_fail (XFCE_IS_SN_BOX (box));
  g_return_if_fail (XFCE_IS_SN_BUTTON (button));
  g_return_if_fail (gtk_widget_get_parent (GTK_WIDGET (child)) == NULL);

  name = sn_button_get_name (button);
  li = g_hash_table_lookup (box->children, name);
  li = g_list_prepend (li, button);
  g_hash_table_replace (box->children, g_strdup (name), li);

  gtk_widget_set_parent (child, GTK_WIDGET (box));

  gtk_widget_queue_resize (GTK_WIDGET (container));
}



static void
sn_box_remove (GtkContainer *container,
               GtkWidget    *child)
{
  SnBox       *box = XFCE_SN_BOX (container);
  SnButton    *button = XFCE_SN_BUTTON (child);
  GList       *li, *li_tmp;
  const gchar *name;

  /* search the child */
  name = sn_button_get_name (button);
  li = g_hash_table_lookup (box->children, name);
  li_tmp = g_list_find (li, button);
  if (G_LIKELY (li_tmp != NULL))
    {
      /* unparent widget */
      li = g_list_remove_link (li, li_tmp);
      g_hash_table_replace (box->children, g_strdup (name), li);
      gtk_widget_unparent (child);

      /* resize, so we update has-hidden */
      gtk_widget_queue_resize (GTK_WIDGET (container));
    }
}



static void
sn_box_forall (GtkContainer *container,
               gboolean      include_internals,
               GtkCallback   callback,
               gpointer      callback_data)
{
  SnBox    *box = XFCE_SN_BOX (container);
  SnButton *button;
  GList    *known_items, *li, *li_int, *li_tmp;

  /* run callback for all children */
  known_items = sn_config_get_known_items (box->config);
  for (li = known_items; li != NULL; li = li->next)
    {
      li_int = g_hash_table_lookup (box->children, li->data);
      for (li_tmp = li_int; li_tmp != NULL; li_tmp = li_tmp->next)
        {
          button = li_tmp->data;
          callback (GTK_WIDGET (button), callback_data);
        }
    }
}



static GType
sn_box_child_type (GtkContainer *container)
{
  return XFCE_TYPE_SN_BUTTON;
}



static void
sn_box_get_preferred_length (GtkWidget *widget,
                             gint      *minimum_length,
                             gint      *natural_length)
{
  SnBox           *box = XFCE_SN_BOX (widget);
  SnButton        *button;
  GList           *known_items, *li, *li_int, *li_tmp;
  gint             panel_size, icon_size, size, nrows;
  gboolean         single_row, square_icons;
  gint             x, index;
  GtkRequisition   child_req;

  panel_size = sn_config_get_panel_size (box->config);
  icon_size = sn_config_get_icon_size (box->config);
  single_row = sn_config_get_single_row (box->config);
  square_icons = sn_config_get_square_icons (box->config);
  icon_size += 2; /* additional padding */
  if (square_icons)
    {
      nrows = MAX (1, sn_config_get_nrows (box->config));
      size = panel_size / (single_row ? 1 : nrows);
    }
  else
    {
      size = MIN (icon_size, panel_size);
      nrows = single_row ? 1 : MAX (1, panel_size / size);
    }

  x = 0;
  index = 0;

  known_items = sn_config_get_known_items (box->config);
  for (li = known_items; li != NULL; li = li->next)
    {
      li_int = g_hash_table_lookup (box->children, li->data);
      for (li_tmp = li_int; li_tmp != NULL; li_tmp = li_tmp->next)
        {
          button = li_tmp->data;
          if (sn_config_is_hidden (box->config,
                                   sn_button_get_name (button)))
            {
              continue;
            }

          gtk_widget_get_preferred_size (GTK_WIDGET (button), NULL, &child_req);

          /* for each first item in row */
          if (index % nrows == 0)
            x += size;
          index += 1;
        }
    }

  if (minimum_length != NULL)
    *minimum_length = x;

  if (natural_length != NULL)
    *natural_length = x;
}



static void
sn_box_get_preferred_width (GtkWidget *widget,
                            gint      *minimum_width,
                            gint      *natural_width)
{
  SnBox *box = XFCE_SN_BOX (widget);
  gint   panel_size;

  if (sn_config_get_panel_orientation (box->config) == GTK_ORIENTATION_HORIZONTAL)
    {
      sn_box_get_preferred_length (widget, minimum_width, natural_width);
    }
  else
    {
      panel_size = sn_config_get_panel_size (box->config);
      if (minimum_width != NULL)
        *minimum_width = panel_size;
      if (natural_width != NULL)
        *natural_width = panel_size;
    }
}



static void
sn_box_get_preferred_height (GtkWidget *widget,
                             gint      *minimum_height,
                             gint      *natural_height)
{
  SnBox *box = XFCE_SN_BOX (widget);
  gint   panel_size;

  if (sn_config_get_panel_orientation (box->config) == GTK_ORIENTATION_VERTICAL)
    {
      sn_box_get_preferred_length (widget, minimum_height, natural_height);
    }
  else
    {
      panel_size = sn_config_get_panel_size (box->config);
      if (minimum_height != NULL)
        *minimum_height = panel_size;
      if (natural_height != NULL)
        *natural_height = panel_size;
    }
}



static void
sn_box_size_allocate (GtkWidget     *widget,
                      GtkAllocation *allocation)
{
  SnBox           *box = XFCE_SN_BOX (widget);
  SnButton        *button;
  GtkAllocation    child_alloc;
  gint             panel_size, icon_size, xsize, ysize, nrows;
  gboolean         single_row, square_icons;
  gint             x, y, row;
  GList           *known_items, *li, *li_int, *li_tmp;
  GtkOrientation   panel_orientation;

  row = 0;
  x = 0;
  y = 0;

  gtk_widget_set_allocation (widget, allocation);

  panel_size = sn_config_get_panel_size (box->config);
  icon_size = sn_config_get_icon_size (box->config);
  single_row = sn_config_get_single_row (box->config);
  square_icons = sn_config_get_square_icons (box->config);
  icon_size += 2; /* additional padding */
  if (square_icons)
    {
      nrows = MAX (1, sn_config_get_nrows (box->config));
      xsize = ysize = panel_size / (single_row ? 1 : nrows);
    }
  else
    {
      xsize = MIN (icon_size, panel_size);
      nrows = single_row ? 1 : MAX (1, panel_size / xsize);
      ysize = panel_size / nrows;
    }

  panel_orientation = sn_config_get_panel_orientation (box->config);

  known_items = sn_config_get_known_items (box->config);
  for (li = known_items; li != NULL; li = li->next)
    {
      li_int = g_hash_table_lookup (box->children, li->data);
      for (li_tmp = li_int; li_tmp != NULL; li_tmp = li_tmp->next)
        {
          button = li_int->data;
          if (sn_config_is_hidden (box->config,
                                   sn_button_get_name (button)))
            {
              continue;
            }

          if (nrows == 1)
            y = (panel_size - ysize + 1) / 2;
          else
            y = (2 * row * (panel_size - ysize) + nrows - 1) / (2 * nrows - 2);

          if (panel_orientation == GTK_ORIENTATION_HORIZONTAL)
            {
              child_alloc.x = allocation->x + x;
              child_alloc.y = allocation->y + y;
              child_alloc.width = xsize;
              child_alloc.height = ysize;
            }
          else
            {
              child_alloc.x = allocation->x + y;
              child_alloc.y = allocation->y + x;
              child_alloc.width = ysize;
              child_alloc.height = xsize;
            }

          gtk_widget_size_allocate (GTK_WIDGET (button), &child_alloc);

          row += 1;
          if (row >= nrows)
            {
              x += xsize;
              row = 0;
            }
        }
    }
}



void
sn_box_remove_item (SnBox  *box,
                    SnItem *item)
{
  SnButton *button;
  GList    *known_items, *li, *li_int, *li_tmp;

  g_return_if_fail (XFCE_IS_SN_BOX (box));

  known_items = sn_config_get_known_items (box->config);
  for (li = known_items; li != NULL; li = li->next)
    {
      li_int = g_hash_table_lookup (box->children, li->data);
      for (li_tmp = li_int; li_tmp != NULL; li_tmp = li_tmp->next)
        {
          button = li_tmp->data;
          if (sn_button_get_item (button) == item)
            {
              gtk_container_remove (GTK_CONTAINER (box), GTK_WIDGET (button));
              return;
            }
        }
    }
}
