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

#ifndef UI_TABCOMP_H
#define UI_TABCOMP_H

#include <functional>

#include <glib.h>
#include <gtk/gtk.h>

using TabCompEnterFunc = std::function<void(const gchar *)>;
using TabCompTabFunc = std::function<void(const gchar *)>;
using TabCompTabAppendFunc = std::function<void(const gchar *, gint)>;

GtkWidget *tab_completion_new_with_history(GtkWidget *parent_box, const gchar *text,
                                           const gchar *history_key, gint max_levels);
void tab_completion_append_to_history(GtkWidget *entry, const gchar *path);

GtkWidget *tab_completion_new(GtkWidget *parent_box, const gchar *text);

void tab_completion_set_enter_func(GtkWidget *entry, const TabCompEnterFunc &enter_func);
void tab_completion_set_tab_func(GtkWidget *entry, const TabCompTabFunc &tab_func);
void tab_completion_set_tab_append_func(GtkWidget *entry, const TabCompTabAppendFunc &tab_append_func);

void tab_completion_add_select_button(GtkWidget *entry, const gchar *title, gboolean folders_only,
                                      const gchar *filter, const gchar *filter_desc, const gchar *shortcuts);

GtkWidget *tab_completion_get_box(GtkWidget *entry);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
