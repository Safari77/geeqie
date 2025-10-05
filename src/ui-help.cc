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

#include "ui-help.h"

#include <cstdio>
#include <cstring>

#include <gdk/gdk.h>
#include <glib-object.h>

#include "compat.h"
#include "intl.h"
#include "main-defines.h"
#include "ui-fileops.h"
#include "ui-misc.h"
#include "window.h"

namespace
{

constexpr gint HELP_WINDOW_WIDTH = 650;
constexpr gint HELP_WINDOW_HEIGHT = 350;

/*
 *-----------------------------------------------------------------------------
 * 'help' window
 *-----------------------------------------------------------------------------
 */

#define SCROLL_MARKNAME "scroll_point"

void help_window_scroll(GtkWidget *text, const gchar *key)
{
	GtkTextBuffer *buffer;
	GtkTextIter iter;
	GtkTextIter start;
	GtkTextIter end;

	if (!text || !key) return;

	g_autofree gchar *needle = g_strdup_printf("[section:%s]", key);

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));
	gtk_text_buffer_get_iter_at_offset(buffer, &iter, 0);

	if (gtk_text_iter_forward_search(&iter, needle, GTK_TEXT_SEARCH_TEXT_ONLY,
					 &start, &end, nullptr))
		{
		gint line;
		GtkTextMark *mark;

		line = gtk_text_iter_get_line(&start);
		gtk_text_buffer_get_iter_at_line_offset(buffer, &iter, line, 0);
		gtk_text_buffer_place_cursor(buffer, &iter);

		/* apparently only scroll_to_mark works when the textview is not visible yet */

		/* if mark exists, move it instead of creating one for every scroll */
		mark = gtk_text_buffer_get_mark(buffer, SCROLL_MARKNAME);
		if (mark)
			{
			gtk_text_buffer_move_mark(buffer, mark, &iter);
			}
		else
			{
			mark = gtk_text_buffer_create_mark(buffer, SCROLL_MARKNAME, &iter, FALSE);
			}
		gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(text), mark, 0.0, TRUE, 0, 0);
		}
}

void help_window_load_text(GtkWidget *text, const gchar *path)
{
	FILE *f;
	gchar s_buf[1024];
	GtkTextBuffer *buffer;
	GtkTextIter iter;
	GtkTextIter start;
	GtkTextIter end;

	if (!text || !path) return;

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));

	gtk_text_buffer_get_bounds(buffer, &start, &end);
	gtk_text_buffer_delete(buffer, &start, &end);

	gtk_text_buffer_get_iter_at_offset(buffer, &iter, 0);

	g_autofree gchar *pathl = path_from_utf8(path);
	f = fopen(pathl, "r");
	if (!f)
		{
		g_autofree gchar *buf = g_strdup_printf(_("Unable to load:\n%s"), path);
		gtk_text_buffer_insert(buffer, &iter, buf, -1);
		}
	else
		{
		while (fgets(s_buf, sizeof(s_buf), f))
			{
			g_autofree gchar *buf = nullptr;
			gint l;

			l = strlen(s_buf);

			if (!g_utf8_validate(s_buf, l, nullptr))
				{
				buf = g_locale_to_utf8(s_buf, l, nullptr, nullptr, nullptr);
				if (!buf) buf = g_strdup("\n");
				}
			gtk_text_buffer_insert_with_tags_by_name(buffer, &iter,
								 (buf) ? buf : s_buf, -1,
								 "monospace", NULL);
			}
		fclose(f);
		}

	gtk_text_buffer_get_iter_at_offset(buffer, &iter, 0);
	gtk_text_buffer_place_cursor(buffer, &iter);
	gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(text), &iter, 0.0, TRUE, 0, 0);
}

gboolean help_window_delete_cb(GtkWidget *widget, GdkEventAny *, gpointer)
{
	gq_gtk_widget_destroy(widget);
	return TRUE;
}

void help_window_close(GtkWidget *, gpointer data)
{
	auto window = static_cast<GtkWidget *>(data);
	gq_gtk_widget_destroy(window);
}

} // namespace

void help_window_set_key(GtkWidget *window, const gchar *key)
{
	GtkWidget *text;

	if (!window) return;

	text = static_cast<GtkWidget *>(g_object_get_data(G_OBJECT(window), "text_widget"));
	if (!text) return;

	gdk_window_raise(gtk_widget_get_window(window));

	if (key) help_window_scroll(text, key);
}

GtkWidget *help_window_new(const gchar *title,
			   const gchar *subclass,
			   const gchar *path, const gchar *key)
{
	GtkWidget *window;
	GtkWidget *text;
	GtkTextBuffer *buffer;
	GtkWidget *vbox;
	GtkWidget *hbox;
	GtkWidget *button;
	GtkWidget *scrolled;

	/* window */

	window = window_new(subclass, nullptr, nullptr, title);
	DEBUG_NAME(window);
	gtk_window_set_resizable(GTK_WINDOW(window), TRUE);
	gtk_window_set_default_size(GTK_WINDOW(window), HELP_WINDOW_WIDTH, HELP_WINDOW_HEIGHT);

	g_signal_connect(G_OBJECT(window), "delete_event",
			 G_CALLBACK(help_window_delete_cb), NULL);

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gq_gtk_container_add(window, vbox);
	gtk_widget_show(vbox);

	g_object_set_data(G_OBJECT(window), "text_vbox", vbox);

	/* text window */

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gq_gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);
	gtk_widget_show(hbox);

	scrolled = gq_gtk_scrolled_window_new(nullptr, nullptr);
	gq_gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gq_gtk_box_pack_start(GTK_BOX(hbox), scrolled, TRUE, TRUE, 0);
	gtk_widget_show(scrolled);

	text = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
	gq_gtk_container_add(scrolled, text);
	gtk_widget_show(text);

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));
	gtk_text_buffer_create_tag(buffer, "monospace",
				   "family", "monospace", NULL);

	hbox = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), PREF_PAD_BORDER);
	gtk_button_box_set_layout(GTK_BUTTON_BOX(hbox), GTK_BUTTONBOX_END);
	gq_gtk_box_pack_end(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);

	button = gtk_button_new_from_icon_name(GQ_ICON_CLOSE, GTK_ICON_SIZE_BUTTON);
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(help_window_close), window);
	gq_gtk_container_add(hbox, button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_grab_default(button);
	gtk_widget_show(button);

	g_object_set_data(G_OBJECT(window), "text_widget", text);

	help_window_load_text(text, path);

	gtk_widget_show(window);

	help_window_scroll(text, key);

	return window;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
