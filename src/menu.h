/*
 * Copyright (C) 2004 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: John Ellis
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef MENU_H
#define MENU_H

#include <glib-object.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "typedefs.h"

gpointer submenu_item_get_data(GtkWidget *submenu_item);

GtkWidget *submenu_add_edit(GtkWidget *menu, gboolean sensitive, GList *fd_list, GCallback func, gpointer data);

gchar *sort_type_get_text(SortType method);
bool sort_type_requires_metadata(SortType method);
GtkWidget *submenu_add_sort(GtkWidget *menu, GCallback func, gpointer data,
                            gboolean show_current, SortType type);

GtkWidget *submenu_add_alter(GtkWidget *menu, GCallback func, gpointer data);

GtkWidget *submenu_add_collections(GtkWidget *menu, gboolean sensitive,
                                   GCallback func, gpointer data);

void popup_menu_bar(GtkWidget *widget, GCallback expander_height_cb);
#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
