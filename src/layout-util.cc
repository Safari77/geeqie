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

#include "layout-util.h"

#include <dirent.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <gio/gio.h>
#include <glib-object.h>

#include <config.h>

#include "advanced-exif.h"
#include "archives.h"
#include "bar-keywords.h"
#include "bar-sort.h"
#include "bar.h"
#include "cache-maint.h"
#include "collect-io.h"
#include "collect.h"
#include "color-man.h"
#include "compat.h"
#include "debug.h"
#include "desktop-file.h"
#include "dupe.h"
#include "editors.h"
#include "filedata.h"
#include "fullscreen.h"
#include "histogram.h"
#include "history-list.h"
#include "image-overlay.h"
#include "image.h"
#include "img-view.h"
#include "intl.h"
#include "keymap-template.h"
#include "layout-image.h"
#include "layout.h"
#include "logwindow.h"
#include "main-defines.h"
#include "main.h"
#include "metadata.h"
#include "misc.h"
#include "options.h"
#include "pan-view.h"
#include "pixbuf-renderer.h"
#include "pixbuf-util.h"
#include "preferences.h"
#include "print.h"
#include "rcfile.h"
#include "search-and-run.h"
#include "search.h"
#include "slideshow.h"
#include "toolbar.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-misc.h"
#include "ui-utildlg.h"
#include "utilops.h"
#include "view-dir.h"
#include "view-file.h"
#include "window.h"

static gboolean layout_bar_enabled(LayoutWindow *lw);
static gboolean layout_bar_sort_enabled(LayoutWindow *lw);
static void layout_bars_hide_toggle(LayoutWindow *lw);
static void layout_util_sync_views(LayoutWindow *lw);
static void layout_search_and_run_window_new(LayoutWindow *lw);

/*
 *-----------------------------------------------------------------------------
 * keyboard handler
 *-----------------------------------------------------------------------------
 */

static guint tree_key_overrides[] = {
	GDK_KEY_Page_Up,	GDK_KEY_KP_Page_Up,
	GDK_KEY_Page_Down,	GDK_KEY_KP_Page_Down,
	GDK_KEY_Home,	GDK_KEY_KP_Home,
	GDK_KEY_End,	GDK_KEY_KP_End
};

static gboolean layout_key_match(guint keyval)
{
	guint i;

	for (i = 0; i < sizeof(tree_key_overrides) / sizeof(guint); i++)
		{
		if (keyval == tree_key_overrides[i]) return TRUE;
		}

	return FALSE;
}

void keyboard_scroll_calc(gint &x, gint &y, const GdkEventKey *event)
{
	static gint delta = 0;
	static guint32 time_old = 0;
	static guint keyval_old = 0;

	if (event->state & GDK_SHIFT_MASK)
		{
		x *= 3;
		y *= 3;
		}

	if (event->state & GDK_CONTROL_MASK)
		{
		if (x < 0) x = G_MININT / 2;
		if (x > 0) x = G_MAXINT / 2;
		if (y < 0) y = G_MININT / 2;
		if (y > 0) y = G_MAXINT / 2;

		return;
		}

	if (options->progressive_key_scrolling)
		{
		guint32 time_diff;

		time_diff = event->time - time_old;

		/* key pressed within 125ms ? (1/8 second) */
		if (time_diff > 125 || event->keyval != keyval_old) delta = 0;

		time_old = event->time;
		keyval_old = event->keyval;

		delta += 2;
		}
	else
		{
		delta = 8;
		}

	x *= delta * options->keyboard_scroll_step;
	y *= delta * options->keyboard_scroll_step;
}

gboolean layout_key_press_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	GtkWidget *focused;
	gboolean stop_signal = FALSE;
	gint x = 0;
	gint y = 0;

	if (lw->path_entry && gtk_widget_has_focus(lw->path_entry))
		{
		if (event->keyval == GDK_KEY_Escape && lw->dir_fd)
			{
			gq_gtk_entry_set_text(GTK_ENTRY(lw->path_entry), lw->dir_fd->path);
			}

		/* the gtkaccelgroup of the window is stealing presses before they get to the entry (and more),
		 * so when the some widgets have focus, give them priority (HACK)
		 */
		if (gtk_widget_event(lw->path_entry, reinterpret_cast<GdkEvent *>(event)))
			{
			return TRUE;
			}
		}

	if (lw->vf->file_filter.combo && gtk_widget_has_focus(gtk_bin_get_child(GTK_BIN(lw->vf->file_filter.combo))))
		{
		if (gtk_widget_event(gtk_bin_get_child(GTK_BIN(lw->vf->file_filter.combo)), reinterpret_cast<GdkEvent *>(event)))
			{
			return TRUE;
			}
		}

	if (lw->vd && lw->options.dir_view_type == DIRVIEW_TREE && gtk_widget_has_focus(lw->vd->view) &&
	    !layout_key_match(event->keyval) &&
	    gtk_widget_event(lw->vd->view, reinterpret_cast<GdkEvent *>(event)))
		{
		return TRUE;
		}
	if (lw->bar &&
	    bar_event(lw->bar, reinterpret_cast<GdkEvent *>(event)))
		{
		return TRUE;
		}

	focused = gtk_container_get_focus_child(GTK_CONTAINER(lw->image->widget));
	if (lw->image &&
	    ((focused && gtk_widget_has_focus(focused)) || (lw->tools && widget == lw->window) || lw->full_screen) )
		{
		stop_signal = TRUE;
		switch (event->keyval)
			{
			case GDK_KEY_Left: case GDK_KEY_KP_Left:
				x -= 1;
				break;
			case GDK_KEY_Right: case GDK_KEY_KP_Right:
				x += 1;
				break;
			case GDK_KEY_Up: case GDK_KEY_KP_Up:
				y -= 1;
				break;
			case GDK_KEY_Down: case GDK_KEY_KP_Down:
				y += 1;
				break;
			default:
				stop_signal = FALSE;
				break;
			}

		if (!stop_signal &&
		    !(event->state & GDK_CONTROL_MASK))
			{
			stop_signal = TRUE;
			switch (event->keyval)
				{
				case GDK_KEY_Menu:
					layout_image_menu_popup(lw);
					break;
				default:
					stop_signal = FALSE;
					break;
				}
			}
		}

	if (x != 0 || y!= 0)
		{
		keyboard_scroll_calc(x, y, event);
		layout_image_scroll(lw, x, y, (event->state & GDK_SHIFT_MASK));
		}

	return stop_signal;
}

void layout_keyboard_init(LayoutWindow *lw, GtkWidget *window)
{
	g_signal_connect(G_OBJECT(window), "key_press_event",
			 G_CALLBACK(layout_key_press_cb), lw);
}

/*
 *-----------------------------------------------------------------------------
 * menu callbacks
 *-----------------------------------------------------------------------------
 */


static GtkWidget *layout_window(LayoutWindow *lw)
{
	return lw->full_screen ? lw->full_screen->window : lw->window;
}

static void layout_exit_fullscreen(LayoutWindow *lw)
{
	if (!lw->full_screen) return;
	layout_image_full_screen_stop(lw);
}

static void clear_marks_cancel_cb(GenericDialog *gd, gpointer)
{
	generic_dialog_close(gd);
}

static void clear_marks_help_cb(GenericDialog *, gpointer)
{
	help_window_show("GuideMainWindowMenus.html");
}

static void layout_menu_clear_marks_ok_cb(GenericDialog *gd, gpointer)
{
	marks_clear_all();
	generic_dialog_close(gd);
}

static void layout_menu_clear_marks_cb(GtkAction *, gpointer)
{
	GenericDialog *gd;

	gd = generic_dialog_new(_("Clear Marks"),
				"marks_clear", nullptr, FALSE, clear_marks_cancel_cb, nullptr);
	generic_dialog_add_message(gd, GQ_ICON_DIALOG_QUESTION, _("Clear all marks?"), _("This will clear all marks for all images,\nincluding those linked to keywords"), TRUE);
	generic_dialog_add_button(gd, GQ_ICON_OK, "OK", layout_menu_clear_marks_ok_cb, TRUE);
	generic_dialog_add_button(gd, GQ_ICON_HELP, _("Help"),
				clear_marks_help_cb, FALSE);

	gtk_widget_show(gd->dialog);
}

static void layout_menu_new_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	collection_window_new(nullptr);
}

static void layout_menu_open_cb(GtkAction *widget, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gchar *path;
	gint n;
	GList *collection_list = nullptr;

	layout_exit_fullscreen(lw);

	n = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "recent_index"));
	collect_manager_list(nullptr, nullptr, &collection_list);

	path = static_cast<gchar *>(g_list_nth_data(collection_list, n));

	if (path)
		{
		/* make a copy of it */
		path = g_strdup(path);
		collection_window_new(path);
		g_free(path);
		}

	g_list_free_full(collection_list, g_free);
}

static void layout_menu_search_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	search_new(lw->dir_fd, layout_image_get_fd(lw));
}

static void layout_menu_dupes_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	dupe_window_new();
}

static void layout_menu_pan_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	pan_window_new(lw->dir_fd);
}

static void layout_menu_print_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	print_window_new(layout_image_get_fd(lw), layout_selection_list(lw), layout_list(lw), layout_window(lw));
}

static void layout_menu_dir_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (lw->vd) vd_new_folder(lw->vd, lw->dir_fd);
}

static void layout_menu_copy_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_copy(nullptr, layout_selection_list(lw), nullptr, layout_window(lw));
}

static void layout_menu_copy_path_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_path_list_to_clipboard(layout_selection_list(lw), TRUE, ClipboardAction::COPY);
}

static void layout_menu_copy_path_unquoted_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_path_list_to_clipboard(layout_selection_list(lw), FALSE, ClipboardAction::COPY);
}

static void layout_menu_copy_image_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	ImageWindow *imd = lw->image;

	GdkPixbuf *pixbuf;
	pixbuf = image_get_pixbuf(imd);
	if (!pixbuf) return;
	gtk_clipboard_set_image(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), pixbuf);
}

static void layout_menu_cut_path_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_path_list_to_clipboard(layout_selection_list(lw), FALSE, ClipboardAction::CUT);
}

static void layout_menu_move_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_move(nullptr, layout_selection_list(lw), nullptr, layout_window(lw));
}

static void layout_menu_rename_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_rename(nullptr, layout_selection_list(lw), layout_window(lw));
}

static void layout_menu_delete_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	options->file_ops.safe_delete_enable = FALSE;
	file_util_delete(nullptr, layout_selection_list(lw), layout_window(lw));
}

static void layout_menu_move_to_trash_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	options->file_ops.safe_delete_enable = TRUE;
	file_util_delete(nullptr, layout_selection_list(lw), layout_window(lw));
}

static void layout_menu_move_to_trash_key_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (options->file_ops.enable_delete_key)
		{
		options->file_ops.safe_delete_enable = TRUE;
		file_util_delete(nullptr, layout_selection_list(lw), layout_window(lw));
		}
}

static void layout_menu_disable_grouping_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_data_disable_grouping_list(layout_selection_list(lw), TRUE);
}

static void layout_menu_enable_grouping_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_data_disable_grouping_list(layout_selection_list(lw), FALSE);
}

void layout_menu_close_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	layout_close(lw);
}

static void layout_menu_exit_cb(GtkAction *, gpointer)
{
	exit_program();
}

static void layout_menu_alter_90_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_alter_orientation(lw, ALTER_ROTATE_90);
}

static void layout_menu_rating_0_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_rating(lw, "0");
}

static void layout_menu_rating_1_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_rating(lw, "1");
}

static void layout_menu_rating_2_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_rating(lw, "2");
}

static void layout_menu_rating_3_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_rating(lw, "3");
}

static void layout_menu_rating_4_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_rating(lw, "4");
}

static void layout_menu_rating_5_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_rating(lw, "5");
}

static void layout_menu_rating_m1_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_rating(lw, "-1");
}

static void layout_menu_alter_90cc_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_alter_orientation(lw, ALTER_ROTATE_90_CC);
}

static void layout_menu_alter_180_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_alter_orientation(lw, ALTER_ROTATE_180);
}

static void layout_menu_alter_mirror_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_alter_orientation(lw, ALTER_MIRROR);
}

static void layout_menu_alter_flip_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_alter_orientation(lw, ALTER_FLIP);
}

static void layout_menu_alter_desaturate_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_set_desaturate(lw, gq_gtk_toggle_action_get_active(action));
}

static void layout_menu_alter_ignore_alpha_cb(GtkToggleAction *action, gpointer data)
{
   auto lw = static_cast<LayoutWindow *>(data);

	if (lw->options.ignore_alpha == gq_gtk_toggle_action_get_active(action)) return;

   layout_image_set_ignore_alpha(lw, gq_gtk_toggle_action_get_active(action));
}

static void layout_menu_alter_none_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_alter_orientation(lw, ALTER_NONE);
}

static void layout_menu_exif_rotate_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	options->image.exif_rotate_enable = gq_gtk_toggle_action_get_active(action);
	layout_image_reset_orientation(lw);
}

static void layout_menu_select_rectangle_cb(GtkToggleAction *action, gpointer)
{
	options->draw_rectangle = gq_gtk_toggle_action_get_active(action);
}

static void layout_menu_split_pane_sync_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	lw->options.split_pane_sync = gq_gtk_toggle_action_get_active(action);
}

static void layout_menu_select_overunderexposed_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_set_overunderexposed(lw, gq_gtk_toggle_action_get_active(action));
}

static void layout_menu_write_rotate(GtkToggleAction *, gpointer data, gboolean keep_date)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (!layout_valid(&lw)) return;
	if (!lw || !lw->vf) return;

	const gchar *keep_date_arg = keep_date ? "-t" : "";

	vf_selection_foreach(lw->vf, [keep_date_arg](FileData *fd_n)
	{
		gchar *command = g_strdup_printf("%s/geeqie-rotate -r %d %s \"%s\"",
		                                 gq_bindir, fd_n->user_orientation, keep_date_arg, fd_n->path);
		int cmdstatus = runcmd(command);
		gint run_result = WEXITSTATUS(cmdstatus);
		if (!run_result)
			{
			fd_n->user_orientation = 0;
			}
		else
			{
			GString *message = g_string_new(_("Operation failed:\n"));

			if (run_result == 1)
				message = g_string_append(message, _("No file extension\n"));
			else if (run_result == 3)
				message = g_string_append(message, _("Cannot create tmp file\n"));
			else if (run_result == 4)
				message = g_string_append(message, _("Operation not supported for filetype\n"));
			else if (run_result == 5)
				message = g_string_append(message, _("File is not writable\n"));
			else if (run_result == 6)
				message = g_string_append(message, _("Exiftran error\n"));
			else if (run_result == 7)
				message = g_string_append(message, _("Mogrify error\n"));

			message = g_string_append(message, fd_n->name);

			GenericDialog *gd = generic_dialog_new(_("Image orientation"), "image_orientation", nullptr, TRUE, nullptr, nullptr);
			generic_dialog_add_message(gd, GQ_ICON_DIALOG_ERROR, _("Image orientation"), message->str, TRUE);
			generic_dialog_add_button(gd, GQ_ICON_OK, "OK", nullptr, TRUE);

			gtk_widget_show(gd->dialog);

			g_string_free(message, TRUE);
			}

		g_free(command);
	});
}

static void layout_menu_write_rotate_keep_date_cb(GtkToggleAction *action, gpointer data)
{
	layout_menu_write_rotate(action, data, TRUE);
}

static void layout_menu_write_rotate_cb(GtkToggleAction *action, gpointer data)
{
	layout_menu_write_rotate(action, data, FALSE);
}

static void layout_menu_config_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	show_config_window(lw);
}

static void layout_menu_editors_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	show_editor_list_window();
}

static void layout_menu_layout_config_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	layout_show_config_window(lw);
}

static void layout_menu_remove_thumb_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	cache_manager_show();
}

static void layout_menu_wallpaper_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_to_root(lw);
}

/* single window zoom */
static void layout_menu_zoom_in_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_adjust(lw, get_zoom_increment(), FALSE);
}

static void layout_menu_zoom_out_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_adjust(lw, -get_zoom_increment(), FALSE);
}

static void layout_menu_zoom_1_1_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, 1.0, FALSE);
}

static void layout_menu_zoom_fit_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, 0.0, FALSE);
}

static void layout_menu_zoom_fit_hor_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set_fill_geometry(lw, FALSE, FALSE);
}

static void layout_menu_zoom_fit_vert_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set_fill_geometry(lw, TRUE, FALSE);
}

static void layout_menu_zoom_2_1_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, 2.0, FALSE);
}

static void layout_menu_zoom_3_1_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, 3.0, FALSE);
}
static void layout_menu_zoom_4_1_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, 4.0, FALSE);
}

static void layout_menu_zoom_1_2_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, -2.0, FALSE);
}

static void layout_menu_zoom_1_3_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, -3.0, FALSE);
}

static void layout_menu_zoom_1_4_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, -4.0, FALSE);
}

/* connected zoom */
static void layout_menu_connect_zoom_in_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_adjust(lw, get_zoom_increment(), TRUE);
}

static void layout_menu_connect_zoom_out_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_adjust(lw, -get_zoom_increment(), TRUE);
}

static void layout_menu_connect_zoom_1_1_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, 1.0, TRUE);
}

static void layout_menu_connect_zoom_fit_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, 0.0, TRUE);
}

static void layout_menu_connect_zoom_fit_hor_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set_fill_geometry(lw, FALSE, TRUE);
}

static void layout_menu_connect_zoom_fit_vert_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set_fill_geometry(lw, TRUE, TRUE);
}

static void layout_menu_connect_zoom_2_1_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, 2.0, TRUE);
}

static void layout_menu_connect_zoom_3_1_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, 3.0, TRUE);
}
static void layout_menu_connect_zoom_4_1_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, 4.0, TRUE);
}

static void layout_menu_connect_zoom_1_2_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, -2.0, TRUE);
}

static void layout_menu_connect_zoom_1_3_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, -3.0, TRUE);
}

static void layout_menu_connect_zoom_1_4_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, -4.0, TRUE);
}

static void layout_menu_zoom_to_rectangle_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint x1;
	gint x2;
	gint y1;
	gint y2;

	image_get_rectangle(x1, y1, x2, y2);

	auto *pr = reinterpret_cast<PixbufRenderer *>(lw->image->pr);

	gint image_width = x2 - x1;
	gint image_height = y2 - y1;

	gdouble zoom_width = static_cast<gdouble>(pr->vis_width) / image_width;
	gdouble zoom_height = static_cast<gdouble>(pr->vis_height) / image_height;

	const GdkRectangle rect = pr_coords_map_orientation_reverse(pr->orientation,
	                                                            {x1, y1, image_width, image_height},
	                                                            pr->image_width, pr->image_height);

	gint center_x = (rect.width / 2) + rect.x;
	gint center_y = (rect.height / 2) + rect.y;

	layout_image_zoom_set(lw, zoom_width > zoom_height ? zoom_height : zoom_width, FALSE);
	image_scroll_to_point(lw->image, center_x, center_y, 0.5, 0.5);
}

static void layout_menu_split_cb(GtkRadioAction *action, GtkRadioAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	ImageSplitMode mode;

	layout_exit_fullscreen(lw);
	mode = static_cast<ImageSplitMode>(gq_gtk_radio_action_get_current_value(action));
	layout_split_change(lw, mode);
}


static void layout_menu_thumb_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_thumb_set(lw, gq_gtk_toggle_action_get_active(action));
}


static void layout_menu_list_cb(GtkRadioAction *action, GtkRadioAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	layout_views_set(lw, lw->options.dir_view_type, static_cast<FileViewType>(gq_gtk_radio_action_get_current_value(action)));
}

static void layout_menu_view_dir_as_cb(GtkToggleAction *action,  gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);

	if (gq_gtk_toggle_action_get_active(action))
		{
		layout_views_set(lw, DIRVIEW_TREE, lw->options.file_view_type);
		}
	else
		{
		layout_views_set(lw, DIRVIEW_LIST, lw->options.file_view_type);
		}
}

static void layout_menu_view_in_new_window_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	view_window_new(layout_image_get_fd(lw));
}

struct OpenWithData
{
	GAppInfo *application;
	GList *g_file_list;
	GtkWidget *app_chooser_dialog;
};

static void open_with_response_cb(GtkDialog *, gint response_id, gpointer data)
{
	GError *error = nullptr;
	auto open_with_data = static_cast<OpenWithData *>(data);

	if (response_id == GTK_RESPONSE_OK)
		{
		g_app_info_launch(open_with_data->application, open_with_data->g_file_list, nullptr, &error);

		if (error)
			{
			log_printf("Error launching app: %s\n", error->message);
			g_error_free(error);
			}
		}

	g_object_unref(open_with_data->application);
	g_object_unref(g_list_first(open_with_data->g_file_list)->data);
	g_list_free(open_with_data->g_file_list);
	gq_gtk_widget_destroy(GTK_WIDGET(open_with_data->app_chooser_dialog));
	g_free(open_with_data);
}

static void open_with_application_selected_cb(GtkAppChooserWidget *, GAppInfo *application, gpointer data)
{
	auto open_with_data = static_cast<OpenWithData *>(data);

	g_object_unref(open_with_data->application);

	open_with_data->application = g_app_info_dup(application);
}

static void open_with_application_activated_cb(GtkAppChooserWidget *, GAppInfo *application, gpointer data)
{
	GError *error = nullptr;
	auto open_with_data = static_cast<OpenWithData *>(data);

	g_app_info_launch(application, open_with_data->g_file_list, nullptr, &error);

	if (error)
		{
		log_printf("Error launching app.: %s\n", error->message);
		g_error_free(error);
		}

	g_object_unref(open_with_data->application);
	g_object_unref(g_list_first(open_with_data->g_file_list)->data);
	g_list_free(open_with_data->g_file_list);
	gq_gtk_widget_destroy(GTK_WIDGET(open_with_data->app_chooser_dialog));
	g_free(open_with_data);
}

static void layout_menu_open_with_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	FileData *fd;
	GtkWidget *widget;
	OpenWithData *open_with_data;

	if (layout_selection_list(lw))
		{
		open_with_data = g_new(OpenWithData, 1);

		fd = static_cast<FileData *>(g_list_first(layout_selection_list(lw))->data);

		open_with_data->g_file_list = g_list_append(nullptr, g_file_new_for_path(fd->path));

		open_with_data->app_chooser_dialog = gtk_app_chooser_dialog_new(nullptr, GTK_DIALOG_DESTROY_WITH_PARENT, G_FILE(g_list_first(open_with_data->g_file_list)->data));

		widget = gtk_app_chooser_dialog_get_widget(GTK_APP_CHOOSER_DIALOG(open_with_data->app_chooser_dialog));

		open_with_data->application = gtk_app_chooser_get_app_info(GTK_APP_CHOOSER(open_with_data->app_chooser_dialog));

		g_signal_connect(G_OBJECT(widget), "application-selected", G_CALLBACK(open_with_application_selected_cb), open_with_data);
		g_signal_connect(G_OBJECT(widget), "application-activated", G_CALLBACK(open_with_application_activated_cb), open_with_data);
		g_signal_connect(G_OBJECT(open_with_data->app_chooser_dialog), "response", G_CALLBACK(open_with_response_cb), open_with_data);
		g_signal_connect(G_OBJECT(open_with_data->app_chooser_dialog), "close", G_CALLBACK(open_with_response_cb), open_with_data);

		gtk_widget_show(open_with_data->app_chooser_dialog);
		}
}

static void layout_menu_open_archive_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	LayoutWindow *lw_new;
	gchar *dest_dir;
	FileData *fd;

	layout_exit_fullscreen(lw);
	fd = layout_image_get_fd(lw);

	if (fd->format_class == FORMAT_CLASS_ARCHIVE)
		{
		dest_dir = open_archive(layout_image_get_fd(lw));
		if (dest_dir)
			{
			lw_new = layout_new_from_default();
			layout_set_path(lw_new, dest_dir);
			g_free(dest_dir);
			}
		else
			{
			warning_dialog(_("Cannot open archive file"), _("See the Log Window"), GQ_ICON_DIALOG_WARNING, nullptr);
			}
		}
}

static void layout_menu_fullscreen_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_full_screen_toggle(lw);
}

static void layout_menu_escape_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
}

static void layout_menu_overlay_toggle_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	image_osd_toggle(lw->image);
	layout_util_sync_views(lw);
}


static void layout_menu_overlay_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (gq_gtk_toggle_action_get_active(action))
		{
		OsdShowFlags flags = image_osd_get(lw->image);

		if ((flags | OSD_SHOW_INFO | OSD_SHOW_STATUS) != flags)
			image_osd_set(lw->image, static_cast<OsdShowFlags>(flags | OSD_SHOW_INFO | OSD_SHOW_STATUS));
		}
	else
		{
		GtkToggleAction *histogram_action = GTK_TOGGLE_ACTION(gq_gtk_action_group_get_action(lw->action_group, "ImageHistogram"));

		image_osd_set(lw->image, OSD_SHOW_NOTHING);
		gq_gtk_toggle_action_set_active(histogram_action, FALSE); /* this calls layout_menu_histogram_cb */
		}
}

static void layout_menu_histogram_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (gq_gtk_toggle_action_get_active(action))
		{
		image_osd_set(lw->image, static_cast<OsdShowFlags>(OSD_SHOW_INFO | OSD_SHOW_STATUS | OSD_SHOW_HISTOGRAM));
		layout_util_sync_views(lw); /* show the overlay state, default channel and mode in the menu */
		}
	else
		{
		OsdShowFlags flags = image_osd_get(lw->image);
		if (flags & OSD_SHOW_HISTOGRAM)
			image_osd_set(lw->image, static_cast<OsdShowFlags>(flags & ~OSD_SHOW_HISTOGRAM));
		}
}

static void layout_menu_animate_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (lw->options.animate == gq_gtk_toggle_action_get_active(action)) return;
	layout_image_animate_toggle(lw);
}

static void layout_menu_rectangular_selection_cb(GtkToggleAction *action, gpointer)
{
	options->collections.rectangular_selection = gq_gtk_toggle_action_get_active(action);
}

static void layout_menu_histogram_toggle_channel_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	image_osd_histogram_toggle_channel(lw->image);
	layout_util_sync_views(lw);
}

static void layout_menu_histogram_toggle_mode_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	image_osd_histogram_toggle_mode(lw->image);
	layout_util_sync_views(lw);
}

static void layout_menu_histogram_channel_cb(GtkRadioAction *action, GtkRadioAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint channel = gq_gtk_radio_action_get_current_value(action);
	GtkToggleAction *histogram_action = GTK_TOGGLE_ACTION(gq_gtk_action_group_get_action(lw->action_group, "ImageHistogram"));

	if (channel < 0 || channel >= HCHAN_COUNT) return;

	gq_gtk_toggle_action_set_active(histogram_action, TRUE); /* this calls layout_menu_histogram_cb */
	image_osd_histogram_set_channel(lw->image, channel);
}

static void layout_menu_histogram_mode_cb(GtkRadioAction *action, GtkRadioAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint mode = gq_gtk_radio_action_get_current_value(action);
	GtkToggleAction *histogram_action = GTK_TOGGLE_ACTION(gq_gtk_action_group_get_action(lw->action_group, "ImageHistogram"));

	if (mode < 0 || mode > 1) return;

	gq_gtk_toggle_action_set_active(histogram_action, TRUE); /* this calls layout_menu_histogram_cb */
	image_osd_histogram_set_mode(lw->image, mode);
}

static void layout_menu_refresh_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_refresh(lw);
}

static void layout_menu_bar_exif_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	layout_exif_window_new(lw);
}

static void layout_menu_search_and_run_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	layout_search_and_run_window_new(lw);
}


static void layout_menu_float_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (lw->options.tools_float == gq_gtk_toggle_action_get_active(action)) return;

	layout_exit_fullscreen(lw);
	layout_tools_float_toggle(lw);
}

static void layout_menu_hide_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	layout_tools_hide_toggle(lw);
}

static void layout_menu_selectable_toolbars_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (lw->options.selectable_toolbars_hidden == gq_gtk_toggle_action_get_active(action)) return;

	layout_exit_fullscreen(lw);
	layout_selectable_toolbars_toggle(lw);
}

static void layout_menu_info_pixel_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (lw->options.show_info_pixel == gq_gtk_toggle_action_get_active(action)) return;

	layout_exit_fullscreen(lw);
	layout_info_pixel_set(lw, !lw->options.show_info_pixel);
}

/* NOTE: these callbacks are called also from layout_util_sync_views */
static void layout_menu_bar_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (layout_bar_enabled(lw) == gq_gtk_toggle_action_get_active(action)) return;

	layout_exit_fullscreen(lw);
	layout_bar_toggle(lw);
}

static void layout_menu_bar_sort_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (layout_bar_sort_enabled(lw) == gq_gtk_toggle_action_get_active(action)) return;

	layout_exit_fullscreen(lw);
	layout_bar_sort_toggle(lw);
}

static void layout_menu_hide_bars_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (lw->options.bars_state.hidden == gq_gtk_toggle_action_get_active(action))
		{
		return;
		}
	layout_bars_hide_toggle(lw);
}

static void layout_menu_slideshow_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (layout_image_slideshow_active(lw) == gq_gtk_toggle_action_get_active(action)) return;
	layout_image_slideshow_toggle(lw);
}

static void layout_menu_slideshow_pause_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_slideshow_pause_toggle(lw);
}

static void layout_menu_slideshow_slower_cb(GtkAction *, gpointer)
{
	options->slideshow.delay = std::min<gdouble>(options->slideshow.delay + 5, SLIDESHOW_MAX_SECONDS);
}

static void layout_menu_slideshow_faster_cb(GtkAction *, gpointer)
{
	options->slideshow.delay = std::max<gdouble>(options->slideshow.delay - 5, SLIDESHOW_MIN_SECONDS * 10);
}


static void layout_menu_stereo_mode_next_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint mode = layout_image_stereo_pixbuf_get(lw);

	/* 0->1, 1->2, 2->3, 3->1 - disable auto, then cycle */
	mode = mode % 3 + 1;

	GtkAction *radio = gq_gtk_action_group_get_action(lw->action_group, "StereoAuto");
	gq_gtk_radio_action_set_current_value(GTK_RADIO_ACTION(radio), mode);

	/*
	this is called via fallback in layout_menu_stereo_mode_cb
	layout_image_stereo_pixbuf_set(lw, mode);
	*/

}

static void layout_menu_stereo_mode_cb(GtkRadioAction *action, GtkRadioAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint mode = gq_gtk_radio_action_get_current_value(action);
	layout_image_stereo_pixbuf_set(lw, mode);
}

static void layout_menu_draw_rectangle_aspect_ratio_cb(GtkRadioAction *action, GtkRadioAction *, gpointer)
{
	options->rectangle_draw_aspect_ratio = static_cast<RectangleDrawAspectRatio>(gq_gtk_radio_action_get_current_value(action));
}

static void layout_menu_help_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	help_window_show("index.html");
}

static void layout_menu_help_search_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	help_search_window_show();
}

static void layout_menu_help_pdf_cb(GtkAction *, gpointer)
{
	help_pdf();
}

static void layout_menu_help_keys_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	help_window_show("GuideReferenceKeyboardShortcuts.html");
}

static void layout_menu_notes_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	help_window_show("release_notes");
}

static void layout_menu_changelog_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	help_window_show("changelog");
}

static constexpr const char *keyboard_map_hardcoded[][2] = {
	{"Scroll","Left"},
	{"FastScroll", "&lt;Shift&gt;Left"},
	{"Left Border", "&lt;Primary&gt;Left"},
	{"Left Border", "&lt;Primary&gt;&lt;Shift&gt;Left"},
	{"Scroll", "Right"},
	{"FastScroll", "&lt;Shift&gt;Right"},
	{"Right Border", "&lt;Primary&gt;Right"},
	{"Right Border", "&lt;Primary&gt;&lt;Shift&gt;Right"},
	{"Scroll", "Up"},
	{"FastScroll", "&lt;Shift&gt;Up"},
	{"Upper Border", "&lt;Primary&gt;Up"},
	{"Upper Border", "&lt;Primary&gt;&lt;Shift&gt;Up"},
	{"Scroll", "Down"},
	{"FastScroll", "&lt;Shift&gt;Down"},
	{"Lower Border", "&lt;Primary&gt;Down"},
	{"Lower Border", "&lt;Primary&gt;&lt;Shift&gt;Down"},
	{"Next/Drag", "M1"},
	{"FastDrag", "&lt;Shift&gt;M1"},
	{"DnD Start", "M2"},
	{"Menu", "M3"},
	{"PrevImage", "MW4"},
	{"NextImage", "MW5"},
	{"ScrollUp", "&lt;Shift&gt;MW4"},
	{"ScrollDown", "&lt;Shift&gt;MW5"},
	{"ZoomIn", "&lt;Primary&gt;MW4"},
	{"ZoomOut", "&lt;Primary&gt;MW5"},
};

static void layout_menu_foreach_func(
					gpointer data,
					const gchar *accel_path,
					guint accel_key,
					GdkModifierType accel_mods,
					gboolean)
{
	gchar *path;
	gchar *name;
	gchar *key_name;
	gchar *menu_name;
	auto array = static_cast<GPtrArray *>(data);

	path = g_strescape(accel_path, nullptr);
	name = gtk_accelerator_name(accel_key, accel_mods);

	menu_name = g_strdup(strrchr(path, '/') + 1);

	if (strrchr(name, '>'))
		{
		g_auto(GStrv) subset_lt_arr = g_strsplit_set(name, "<", 4);
		g_autofree gchar *subset_lt = g_strjoinv("&lt;", subset_lt_arr);
		g_auto(GStrv) subset_gt_arr = g_strsplit_set(subset_lt, ">", 4);

		key_name = g_strjoinv("&gt;", subset_gt_arr);
		}
	else
		key_name = g_strdup(name);

	g_ptr_array_add(array, menu_name);
	g_ptr_array_add(array, key_name);

	g_free(name);
	g_free(path);
}

static void layout_menu_kbd_map_cb(GtkAction *, gpointer)
{
	gint fd = -1;
	char * tmp_file;
	GError *error = nullptr;
	GIOChannel *channel;
	char **pre_key;
	char **post_key;
	const char *key_name;
	char *converted_line;
	int keymap_index;

	fd = g_file_open_tmp("geeqie_keymap_XXXXXX.svg", &tmp_file, &error);
	if (error)
		{
		log_printf("Error: Keyboard Map - cannot create file:%s\n",error->message);
		g_error_free(error);
		}
	else
		{
		GPtrArray *array = g_ptr_array_new_with_free_func(g_free);

		gtk_accel_map_foreach(array, layout_menu_foreach_func);

		channel = g_io_channel_unix_new(fd);

		keymap_index = 0;
		while (keymap_template[keymap_index])
			{
			if (g_strrstr(keymap_template[keymap_index], ">key:"))
				{
				pre_key = g_strsplit(keymap_template[keymap_index],">key:",2);
				post_key = g_strsplit(pre_key[1],"<",2);

				key_name = " ";
				for (guint index = 0; index < array->len-1; index += 2)
					{
					if (!(g_ascii_strcasecmp(static_cast<const gchar *>(g_ptr_array_index(array,index+1)), post_key[0])))
						{
						key_name = static_cast<const gchar *>(g_ptr_array_index(array,index+0));
						break;
						}
					}

				for (const auto& m : keyboard_map_hardcoded)
					{
					if (!(g_strcmp0(m[1], post_key[0])))
						{
						key_name = m[0];
						break;
						}
					}

				converted_line = g_strconcat(pre_key[0], ">", key_name, "<", post_key[1], "\n", NULL);
				g_io_channel_write_chars(channel, converted_line, -1, nullptr, &error);
				if (error) {log_printf("Warning: Keyboard Map:%s\n",error->message); g_error_free(error);}

				g_free(converted_line);
				g_strfreev(pre_key);
				g_strfreev(post_key);
				}
			else
				{
				g_io_channel_write_chars(channel, keymap_template[keymap_index], -1, nullptr, &error);
				if (error) {log_printf("Warning: Keyboard Map:%s\n",error->message); g_error_free(error);}
				g_io_channel_write_chars(channel, "\n", -1, nullptr, &error);
				if (error) {log_printf("Warning: Keyboard Map:%s\n",error->message); g_error_free(error);}
				}
			keymap_index++;
			}

		g_io_channel_flush(channel, &error);
		if (error) {log_printf("Warning: Keyboard Map:%s\n",error->message); g_error_free(error);}
		g_io_channel_unref(channel);

		g_ptr_array_unref(array);

		view_window_new(file_data_new_simple(tmp_file));
		g_free(tmp_file);
		}
}

static void layout_menu_about_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	show_about_window(lw);
}

static void layout_menu_crop_selection_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	start_editor_from_file("org.geeqie.image-crop.desktop", lw->image->image_fd);
}

static void layout_menu_log_window_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_exit_fullscreen(lw);
	log_window_new(lw);
}


/*
 *-----------------------------------------------------------------------------
 * select menu
 *-----------------------------------------------------------------------------
 */

static void layout_menu_select_all_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_select_all(lw);
}

static void layout_menu_unselect_all_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_select_none(lw);
}

static void layout_menu_invert_selection_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_select_invert(lw);
}

static void layout_menu_file_filter_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_file_filter_set(lw, gq_gtk_toggle_action_get_active(action));
}

static void layout_menu_marks_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_marks_set(lw, gq_gtk_toggle_action_get_active(action));
}


static void layout_menu_set_mark_sel_cb(GtkAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint mark = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "mark_num"));
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	layout_selection_to_mark(lw, mark, STM_MODE_SET);
}

static void layout_menu_res_mark_sel_cb(GtkAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint mark = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "mark_num"));
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	layout_selection_to_mark(lw, mark, STM_MODE_RESET);
}

static void layout_menu_toggle_mark_sel_cb(GtkAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint mark = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "mark_num"));
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	layout_selection_to_mark(lw, mark, STM_MODE_TOGGLE);
}

static void layout_menu_sel_mark_cb(GtkAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint mark = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "mark_num"));
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	layout_mark_to_selection(lw, mark, MTS_MODE_SET);
}

static void layout_menu_sel_mark_or_cb(GtkAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint mark = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "mark_num"));
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	layout_mark_to_selection(lw, mark, MTS_MODE_OR);
}

static void layout_menu_sel_mark_and_cb(GtkAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint mark = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "mark_num"));
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	layout_mark_to_selection(lw, mark, MTS_MODE_AND);
}

static void layout_menu_sel_mark_minus_cb(GtkAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint mark = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "mark_num"));
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	layout_mark_to_selection(lw, mark, MTS_MODE_MINUS);
}

static void layout_menu_mark_filter_toggle_cb(GtkAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint mark = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(action), "mark_num"));
	g_assert(mark >= 1 && mark <= FILEDATA_MARKS_SIZE);

	layout_marks_set(lw, TRUE);
	layout_mark_filter_toggle(lw, mark);
}


/*
 *-----------------------------------------------------------------------------
 * go menu
 *-----------------------------------------------------------------------------
 */

static void layout_menu_image_first_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	layout_image_first(lw);
}

static void layout_menu_image_prev_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (lw->options.split_pane_sync)
		{
		for (gint i = 0; i < MAX_SPLIT_IMAGES; i++)
			{
			if (lw->split_images[i])
				{
				DEBUG_1("image activate scroll %d", i);
				layout_image_activate(lw, i, FALSE);
				layout_image_prev(lw);
				}
			}
		}
	else
		{
		layout_image_prev(lw);
		}
}

static void layout_menu_image_next_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (lw->options.split_pane_sync)
		{
		for (gint i = 0; i < MAX_SPLIT_IMAGES; i++)
			{
			if (lw->split_images[i])
				{
				DEBUG_1("image activate scroll %d", i);
				layout_image_activate(lw, i, FALSE);
				layout_image_next(lw);
				}
			}
		}
	else
		{
		layout_image_next(lw);
		}
}

static void layout_menu_page_first_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	FileData *fd = layout_image_get_fd(lw);

	if (fd->page_total > 0)
		{
		file_data_set_page_num(fd, 1);
		}
}

static void layout_menu_page_last_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	FileData *fd = layout_image_get_fd(lw);

	if (fd->page_total > 0)
		{
		file_data_set_page_num(fd, -1);
		}
}

static void layout_menu_page_next_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	FileData *fd = layout_image_get_fd(lw);

	if (fd->page_total > 0)
		{
		file_data_inc_page_num(fd);
		}
}

static void layout_menu_page_previous_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	FileData *fd = layout_image_get_fd(lw);

	if (fd->page_total > 0)
		{
		file_data_dec_page_num(fd);
		}
}

static void layout_menu_image_forward_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	/* Obtain next image */
	layout_set_path(lw, image_chain_forward());
}

static void layout_menu_image_back_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	/* Obtain previous image */
	layout_set_path(lw, image_chain_back());
}

static void layout_menu_split_pane_next_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint active_frame;

	active_frame = lw->active_split_image;

	if (active_frame < MAX_SPLIT_IMAGES-1 && lw->split_images[active_frame+1] )
		{
		active_frame++;
		}
	else
		{
		active_frame = 0;
		}
	layout_image_activate(lw, active_frame, FALSE);
}

static void layout_menu_split_pane_prev_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint active_frame;

	active_frame = lw->active_split_image;

	if (active_frame >=1 && lw->split_images[active_frame-1] )
		{
		active_frame--;
		}
	else
		{
		active_frame = MAX_SPLIT_IMAGES-1;
		while (!lw->split_images[active_frame])
			{
			active_frame--;
			}
		}
	layout_image_activate(lw, active_frame, FALSE);
}

static void layout_menu_split_pane_updown_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint active_frame;

	active_frame = lw->active_split_image;

	if (lw->split_images[MAX_SPLIT_IMAGES-1] )
		{
		active_frame = active_frame ^ 2;
		}
	else
		{
		active_frame = active_frame ^ 1;
		}
	layout_image_activate(lw, active_frame, FALSE);
}

static void layout_menu_image_last_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	layout_image_last(lw);
}

static void layout_menu_back_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	FileData *dir_fd;

	/* Obtain previous path */
	dir_fd = file_data_new_dir(history_chain_back());
	layout_set_fd(lw, dir_fd);
	file_data_unref(dir_fd);
}

static void layout_menu_forward_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	FileData *dir_fd;

	/* Obtain next path */
	dir_fd = file_data_new_dir(history_chain_forward());
	layout_set_fd(lw, dir_fd);
	file_data_unref(dir_fd);
}

static void layout_menu_home_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	const gchar *path;

	if (lw->options.home_path && *lw->options.home_path)
		path = lw->options.home_path;
	else
		path = homedir();

	if (path)
		{
		FileData *dir_fd = file_data_new_dir(path);
		layout_set_fd(lw, dir_fd);
		file_data_unref(dir_fd);
		}
}

static void layout_menu_up_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	ViewDir *vd = lw->vd;
	gchar *path;

	if (!vd->dir_fd || strcmp(vd->dir_fd->path, G_DIR_SEPARATOR_S) == 0) return;
	path = remove_level_from_path(vd->dir_fd->path);

	if (vd->select_func)
		{
		FileData *fd = file_data_new_dir(path);
		vd->select_func(vd, fd, vd->select_data);
		file_data_unref(fd);
		}

	g_free(path);
}


/*
 *-----------------------------------------------------------------------------
 * edit menu
 *-----------------------------------------------------------------------------
 */

static void layout_menu_edit_cb(GtkAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	const gchar *key = gq_gtk_action_get_name(action);

	if (!editor_window_flag_set(key))
		layout_exit_fullscreen(lw);

	file_util_start_editor_from_filelist(key, layout_selection_list(lw), layout_get_path(lw), lw->window);
}


static void layout_menu_metadata_write_cb(GtkAction *, gpointer)
{
	metadata_write_queue_confirm(TRUE, nullptr, nullptr);
}

static GtkWidget *last_focussed = nullptr;
static void layout_menu_keyword_autocomplete_cb(GtkAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	GtkWidget *tmp;
	gboolean auto_has_focus;

	tmp = gtk_window_get_focus(GTK_WINDOW(lw->window));
	auto_has_focus = bar_keywords_autocomplete_focus(lw);

	if (auto_has_focus)
		{
		gtk_widget_grab_focus(last_focussed);
		}
	else
		{
		last_focussed = tmp;
		}
}

/*
 *-----------------------------------------------------------------------------
 * color profile button (and menu)
 *-----------------------------------------------------------------------------
 */
#if HAVE_LCMS
static void layout_color_menu_enable_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (layout_image_color_profile_get_use(lw) == gq_gtk_toggle_action_get_active(action)) return;

	layout_image_color_profile_set_use(lw, gq_gtk_toggle_action_get_active(action));
	layout_util_sync_color(lw);
	layout_image_refresh(lw);
}

static void layout_color_menu_use_image_cb(GtkToggleAction *action, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint input;
	gboolean use_image;

	if (!layout_image_color_profile_get(lw, input, use_image)) return;
	if (use_image == gq_gtk_toggle_action_get_active(action)) return;
	layout_image_color_profile_set(lw, input, gq_gtk_toggle_action_get_active(action));
	layout_util_sync_color(lw);
	layout_image_refresh(lw);
}

static void layout_color_menu_input_cb(GtkRadioAction *action, GtkRadioAction *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint type;
	gint input;
	gboolean use_image;

	type = gq_gtk_radio_action_get_current_value(action);
	if (type < 0 || type >= COLOR_PROFILE_FILE + COLOR_PROFILE_INPUTS) return;

	if (!layout_image_color_profile_get(lw, input, use_image)) return;
	if (type == input) return;

	layout_image_color_profile_set(lw, type, use_image);
	layout_image_refresh(lw);
}
#else
static void layout_color_menu_enable_cb()
{
}

static void layout_color_menu_use_image_cb()
{
}

static void layout_color_menu_input_cb()
{
}
#endif


/*
 *-----------------------------------------------------------------------------
 * recent menu
 *-----------------------------------------------------------------------------
 */

static void layout_menu_recent_cb(GtkWidget *widget, gpointer)
{
	gint n;
	gchar *path;

	n = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "recent_index"));

	path = static_cast<gchar *>(g_list_nth_data(history_list_get_by_key("recent"), n));

	if (!path) return;

	/* make a copy of it */
	path = g_strdup(path);
	collection_window_new(path);
	g_free(path);
}

static void layout_menu_collection_recent_update(LayoutWindow *lw)
{
	GtkWidget *menu;
	GtkWidget *recent;
	GtkWidget *item;
	GList *list;
	gint n;

	if (!lw->ui_manager) return;

	list = history_list_get_by_key("recent");
	n = 0;

	menu = gtk_menu_new();

	while (list)
		{
		const gchar *filename = filename_from_path(static_cast<gchar *>(list->data));
		gchar *name;
		gboolean free_name = FALSE;

		if (file_extension_match(filename, GQ_COLLECTION_EXT))
			{
			name = remove_extension_from_path(filename);
			free_name = TRUE;
			}
		else
			{
			name = const_cast<gchar *>(filename);
			}

		item = menu_item_add_simple(menu, name, G_CALLBACK(layout_menu_recent_cb), lw);
		if (free_name) g_free(name);
		g_object_set_data(G_OBJECT(item), "recent_index", GINT_TO_POINTER(n));
		list = list->next;
		n++;
		}

	if (n == 0)
		{
		menu_item_add(menu, _("Empty"), nullptr, nullptr);
		}

	recent = gq_gtk_ui_manager_get_widget(lw->ui_manager, options->hamburger_menu ? "/MainMenu/OpenMenu/FileMenu/OpenRecent" : "/MainMenu/FileMenu/OpenRecent");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(recent), menu);
	gtk_widget_set_sensitive(recent, (n != 0));
}

static void layout_menu_collection_open_update(LayoutWindow *lw)
{
	gchar *name;
	gint n;
	GList *collection_list = nullptr;
	GList *work;
	GtkWidget *item;
	GtkWidget *menu;
	GtkWidget *recent;

	if (!lw->ui_manager) return;

	collect_manager_list(&collection_list, nullptr, nullptr);

	n = 0;

	menu = gtk_menu_new();

	work = collection_list;
	while (work)
		{
		auto *filename = static_cast<gchar *>(work->data);

		const gboolean has_extension = file_extension_match(filename, GQ_COLLECTION_EXT);
		if (has_extension)
			{
			name = remove_extension_from_path(filename);
			}
		else
			{
			name = filename;
			}

		item = menu_item_add_simple(menu, name, G_CALLBACK(layout_menu_open_cb), lw);
		if (has_extension)
			{
			g_free(name);
			}
		g_object_set_data(G_OBJECT(item), "recent_index", GINT_TO_POINTER(n));
		work = work->next;
		n++;
		}

	g_list_free_full(collection_list, g_free);

	if (n == 0)
		{
		menu_item_add(menu, _("Empty"), nullptr, nullptr);
		}

	recent = gq_gtk_ui_manager_get_widget(lw->ui_manager, options->hamburger_menu ? "/MainMenu/OpenMenu/FileMenu/OpenCollection" : "/MainMenu/FileMenu/OpenCollection");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(recent), menu);
	gtk_widget_set_sensitive(recent, (n != 0));
}

void layout_recent_update_all()
{
	GList *work;

	work = layout_window_list;
	while (work)
		{
		auto lw = static_cast<LayoutWindow *>(work->data);
		work = work->next;

		layout_menu_collection_recent_update(lw);
		layout_menu_collection_open_update(lw);
		}
}

void layout_recent_add_path(const gchar *path)
{
	if (!path) return;

	history_list_add_to_key("recent", path, options->open_recent_list_maxsize);

	layout_recent_update_all();
}

/*
 *-----------------------------------------------------------------------------
 * window layout menu
 *-----------------------------------------------------------------------------
 */
struct WindowNames
{
	gboolean displayed;
	gchar *name;
	gchar *path;
};

struct RenameWindow
{
	GenericDialog *gd;
	LayoutWindow *lw;

	GtkWidget *button_ok;
	GtkWidget *window_name_entry;
};

struct DeleteWindow
{
	GenericDialog *gd;
	LayoutWindow *lw;

	GtkWidget *button_ok;
	GtkWidget *group;
};

static gint layout_window_menu_list_sort_cb(gconstpointer a, gconstpointer b)
{
	auto wna = static_cast<const WindowNames *>(a);
	auto wnb = static_cast<const WindowNames *>(b);

	return g_strcmp0(wna->name, wnb->name);
}

static GList *layout_window_menu_list(GList *listin)
{
	WindowNames *wn;
	DIR *dp;
	struct dirent *dir;
	gchar *pathl;

	pathl = path_from_utf8(get_window_layouts_dir());
	dp = opendir(pathl);
	if (!dp)
		{
		/* dir not found */
		g_free(pathl);
		return listin;
		}

	while ((dir = readdir(dp)) != nullptr)
		{
		gchar *name_file = dir->d_name;

		if (g_str_has_suffix(name_file, ".xml"))
			{
			gchar *name_utf8 = path_to_utf8(name_file);
			gchar *name_base = g_strndup(name_utf8, strlen(name_utf8) - 4);

			wn  = g_new0(WindowNames, 1);
			wn->displayed = g_list_find_custom(layout_window_list, name_base, reinterpret_cast<GCompareFunc>(layout_compare_options_id)) ? TRUE : FALSE;
			wn->name = name_base;
			wn->path = g_build_filename(pathl, name_utf8, NULL);
			listin = g_list_append(listin, wn);

			g_free(name_utf8);
			}
		}
	closedir(dp);

	g_free(pathl);

	return g_list_sort(listin, layout_window_menu_list_sort_cb);
}

static void layout_menu_new_window_cb(GtkWidget *, gpointer data)
{
	gint n;

	n = GPOINTER_TO_INT(data);
	GList *menulist = nullptr;

	menulist = layout_window_menu_list(menulist);
	auto wn = static_cast<WindowNames *>(g_list_nth(menulist, n )->data);

	if (wn->path)
		{
		load_config_from_file(wn->path, FALSE);
		}
	else
		{
		log_printf(_("Error: window layout name: %s does not exist\n"), wn->path);
		}
}

static void layout_menu_new_window_update(LayoutWindow *lw)
{
	GtkWidget *menu;
	GtkWidget *sub_menu;
	GtkWidget *item;
	GList *children;
	GList *iter;
	gint n;
	GList *list = nullptr;
	gint i = 0;
	WindowNames *wn;

	if (!lw->ui_manager) return;

	list = layout_window_menu_list(list);

	menu = gq_gtk_ui_manager_get_widget(lw->ui_manager, options->hamburger_menu ? "/MainMenu/OpenMenu/WindowsMenu/NewWindow" : "/MainMenu/WindowsMenu/NewWindow");
	sub_menu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(menu));

	children = gtk_container_get_children(GTK_CONTAINER(sub_menu));
	for (iter = children; iter != nullptr; iter = g_list_next(iter), i++)
		{
		if (i >= 4) // separator, default, from current, separator
			{
			gq_gtk_widget_destroy(GTK_WIDGET(iter->data));
			}
		}
	g_list_free(children);

	menu_item_add_divider(sub_menu);

	n = 0;
	while (list)
		{
		wn = static_cast<WindowNames *>(list->data);
		item = menu_item_add_simple(sub_menu, wn->name, G_CALLBACK(layout_menu_new_window_cb), GINT_TO_POINTER(n));
		if (wn->displayed)
			{
			gtk_widget_set_sensitive(item, FALSE);
			}
		list = list->next;
		n++;
		}
}

static void window_rename_cancel_cb(GenericDialog *, gpointer data)
{
	auto rw = static_cast<RenameWindow *>(data);

	generic_dialog_close(rw->gd);
	g_free(rw);
}

static void window_rename_ok(GenericDialog *, gpointer data)
{
	auto rw = static_cast<RenameWindow *>(data);
	gchar *path;
	gchar *xml_name;
	gchar *new_id;

	new_id = g_strdup(gq_gtk_entry_get_text(GTK_ENTRY(rw->window_name_entry)));

	const auto window_names_compare_name = [](gconstpointer data, gconstpointer user_data)
	{
		return g_strcmp0(static_cast<const WindowNames *>(data)->name, static_cast<const gchar *>(user_data));
	};

	if (g_list_find_custom(layout_window_menu_list(nullptr), new_id, window_names_compare_name))
		{
		gchar *buf;
		buf = g_strdup_printf(_("Window layout name \"%s\" already exists."), new_id);
		warning_dialog(_("Rename window"), buf, GQ_ICON_DIALOG_WARNING, rw->gd->dialog);
		g_free(buf);
		}
	else
		{
		xml_name = g_strdup_printf("%s.xml", rw->lw->options.id);
		path = g_build_filename(get_window_layouts_dir(), xml_name, NULL);

		if (isfile(path))
			{
			unlink_file(path);
			}
		g_free(xml_name);
		g_free(path);

		g_free(rw->lw->options.id);
		rw->lw->options.id = g_strdup(new_id);
		layout_menu_new_window_update(rw->lw);
		layout_refresh(rw->lw);
		image_update_title(rw->lw->image);
		}

	save_layout(rw->lw);

	g_free(new_id);
	generic_dialog_close(rw->gd);
	g_free(rw);
}

static void window_rename_ok_cb(GenericDialog *gd, gpointer data)
{
	auto rw = static_cast<RenameWindow *>(data);

	window_rename_ok(gd, rw);
}

static void window_rename_entry_activate_cb(GenericDialog *gd, gpointer data)
{
	auto rw = static_cast<RenameWindow *>(data);

	window_rename_ok(gd, rw);
}

static void window_delete_cancel_cb(GenericDialog *, gpointer data)
{
	auto dw = static_cast<DeleteWindow *>(data);

	g_free(dw);
}

static void window_delete_ok_cb(GenericDialog *, gpointer data)
{
	auto dw = static_cast<DeleteWindow *>(data);
	gchar *path;
	gchar *xml_name;

	xml_name = g_strdup_printf("%s.xml", dw->lw->options.id);
	path = g_build_filename(get_window_layouts_dir(), xml_name, NULL);

	layout_close(dw->lw);
	g_free(dw);

	if (isfile(path))
		{
		unlink_file(path);
		}
	g_free(xml_name);
	g_free(path);
}

static void layout_menu_window_default_cb(GtkWidget *, gpointer)
{
	layout_new_from_default();
}

static void layout_menu_windows_menu_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	GtkWidget *menu;
	GtkWidget *sub_menu;
	gchar *menu_label;
	GList *children;
	GList *iter;
	gint i;

	menu = gq_gtk_ui_manager_get_widget(lw->ui_manager, options->hamburger_menu ? "/MainMenu/OpenMenu/WindowsMenu/" : "/MainMenu/WindowsMenu/");

	sub_menu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(menu));

	/* disable Delete for temporary windows */
	if (g_str_has_prefix(lw->options.id, "lw"))
		{
		i = 0;
		children = gtk_container_get_children(GTK_CONTAINER(sub_menu));
		for (iter = children; iter != nullptr; iter = g_list_next(iter), i++)
			{
			menu_label = g_strdup(gtk_menu_item_get_label(GTK_MENU_ITEM(iter->data)));
			if (g_strcmp0(menu_label, _("Delete window")) == 0)
				{
				gtk_widget_set_sensitive(GTK_WIDGET(iter->data), FALSE);
				}
			g_free(menu_label);
			}
		g_list_free(children);
		}
}

static void layout_menu_view_menu_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	GtkWidget *menu;
	GtkWidget *sub_menu;
	gchar *menu_label;
	GList *children;
	GList *iter;
	gint i;
	FileData *fd;

	menu = gq_gtk_ui_manager_get_widget(lw->ui_manager, options->hamburger_menu ? "/MainMenu/OpenMenu/ViewMenu/" : "/MainMenu/ViewMenu/");
	sub_menu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(menu));

	fd = layout_image_get_fd(lw);

	i = 0;
	children = gtk_container_get_children(GTK_CONTAINER(sub_menu));
	for (iter = children; iter != nullptr; iter = g_list_next(iter), i++)
		{
		menu_label = g_strdup(gtk_menu_item_get_label(GTK_MENU_ITEM(iter->data)));
		if (g_strcmp0(menu_label, _("Open archive")) == 0)
			{
			if (fd && fd->format_class == FORMAT_CLASS_ARCHIVE)
				{
				gtk_widget_set_sensitive(GTK_WIDGET(iter->data), TRUE);
				}
			else
				{
				gtk_widget_set_sensitive(GTK_WIDGET(iter->data), FALSE);
				}
			}
		g_free(menu_label);
		}
	g_list_free(children);
}

static gchar *create_tmp_config_file()
{
	gchar *tmp_file;
	g_autoptr(GError) error = nullptr;

	gint fd = g_file_open_tmp("geeqie_layout_name_XXXXXX.xml", &tmp_file, &error);
	if (error)
		{
		log_printf("Error: Window layout - cannot create file: %s\n", error->message);
		return nullptr;
		}

	close(fd);

	return tmp_file;
}

static void change_window_id(const gchar *infile, const gchar *outfile)
{
	g_autoptr(GFile) in_file = g_file_new_for_path(infile);
	g_autoptr(GFileInputStream) in_file_stream = g_file_read(in_file, nullptr, nullptr);
	g_autoptr(GDataInputStream) in_data_stream = g_data_input_stream_new(G_INPUT_STREAM(in_file_stream));

	g_autoptr(GFile) out_file = g_file_new_for_path(outfile);
	g_autoptr(GFileOutputStream) out_file_stream = g_file_append_to(out_file, G_FILE_CREATE_PRIVATE, nullptr, nullptr);
	g_autoptr(GDataOutputStream) out_data_stream = g_data_output_stream_new(G_OUTPUT_STREAM(out_file_stream));

	g_autofree gchar *id_name = layout_get_unique_id();

	gchar *line;
	while ((line = g_data_input_stream_read_line(in_data_stream, nullptr, nullptr, nullptr)))
		{
		g_data_output_stream_put_string(out_data_stream, line, nullptr, nullptr);
		g_data_output_stream_put_string(out_data_stream, "\n", nullptr, nullptr);

		if (g_str_has_suffix(line, "<layout"))
			{
			g_free(line);
			line = g_data_input_stream_read_line(in_data_stream, nullptr, nullptr, nullptr);

			g_data_output_stream_put_string(out_data_stream, "id = \"", nullptr, nullptr);
			g_data_output_stream_put_string(out_data_stream, id_name, nullptr, nullptr);
			g_data_output_stream_put_string(out_data_stream, "\"\n", nullptr, nullptr);
			}

		g_free(line);
		}
}

static void layout_menu_window_from_current_cb(GtkWidget *, gpointer data)
{
	g_autofree gchar *tmp_file_in = create_tmp_config_file();
	if (!tmp_file_in)
		{
		return;
		}

	g_autofree gchar *tmp_file_out = create_tmp_config_file();
	if (!tmp_file_out)
		{
		unlink_file(tmp_file_in);
		return;
		}

	auto *lw = static_cast<LayoutWindow *>(data);
	save_config_to_file(tmp_file_in, options, lw);
	change_window_id(tmp_file_in, tmp_file_out);
	load_config_from_file(tmp_file_out, FALSE);

	unlink_file(tmp_file_in);
	unlink_file(tmp_file_out);
}

static void layout_menu_window_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_menu_new_window_update(lw);
}

static void layout_menu_window_rename_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	RenameWindow *rw;
	GtkWidget *hbox;

	rw = g_new0(RenameWindow, 1);
	rw->lw = lw;

	rw->gd = generic_dialog_new(_("Rename window"), "rename_window", nullptr, FALSE, window_rename_cancel_cb, rw);
	rw->button_ok = generic_dialog_add_button(rw->gd, GQ_ICON_OK, _("OK"), window_rename_ok_cb, TRUE);

	generic_dialog_add_message(rw->gd, nullptr, _("rename window"), nullptr, FALSE);

	hbox = pref_box_new(rw->gd->vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, 0);
	pref_spacer(hbox, PREF_PAD_INDENT);

	hbox = pref_box_new(rw->gd->vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);

	rw->window_name_entry = gtk_entry_new();
	gtk_widget_set_can_focus(rw->window_name_entry, TRUE);
	gtk_editable_set_editable(GTK_EDITABLE(rw->window_name_entry), TRUE);
	gq_gtk_entry_set_text(GTK_ENTRY(rw->window_name_entry), lw->options.id);
	gq_gtk_box_pack_start(GTK_BOX(hbox), rw->window_name_entry, TRUE, TRUE, 0);
	gtk_widget_grab_focus(GTK_WIDGET(rw->window_name_entry));
	gtk_widget_show(rw->window_name_entry);
	g_signal_connect(rw->window_name_entry, "activate", G_CALLBACK(window_rename_entry_activate_cb), rw);

	gtk_widget_show(rw->gd->dialog);
}

static void layout_menu_window_delete_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	DeleteWindow *dw;
	GtkWidget *hbox;

	dw = g_new0(DeleteWindow, 1);
	dw->lw = lw;

	dw->gd = generic_dialog_new(_("Delete window"), "delete_window", nullptr, TRUE, window_delete_cancel_cb, dw);
	dw->button_ok = generic_dialog_add_button(dw->gd, GQ_ICON_OK, _("OK"), window_delete_ok_cb, TRUE);

	generic_dialog_add_message(dw->gd, nullptr, _("Delete window layout"), nullptr, FALSE);

	hbox = pref_box_new(dw->gd->vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, 0);
	pref_spacer(hbox, PREF_PAD_INDENT);
	dw->group = pref_box_new(hbox, TRUE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);

	hbox = pref_box_new(dw->group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	pref_label_new(hbox, (lw->options.id));

	gtk_widget_show(dw->gd->dialog);
}

/*
 *-----------------------------------------------------------------------------
 * menu
 *-----------------------------------------------------------------------------
 */

#define CB G_CALLBACK
/**
 * tooltip is used as the description field in the Help manual shortcuts documentation
 *
 * struct GtkActionEntry:
 *  name, stock_id, label, accelerator, tooltip, callback
 */
static GtkActionEntry menu_entries[] = {
  { "About",                 GQ_ICON_ABOUT,                     N_("_About"),                                           nullptr,               N_("About"),                                           CB(layout_menu_about_cb) },
  { "AlterNone",             PIXBUF_INLINE_ICON_ORIGINAL,       N_("_Original state"),                                  "<shift>O",            N_("Image rotate Original state"),                     CB(layout_menu_alter_none_cb) },
  { "AspectRatioMenu",       nullptr,                           N_("Aspect Ratio"),                                     nullptr,               N_("Aspect Ratio"),                                    nullptr },
  { "Back",                  GQ_ICON_GO_PREV,                   N_("_Back"),                                            nullptr,               N_("Back in folder history"),                          CB(layout_menu_back_cb) },
  { "ClearMarks",            nullptr,                           N_("Clear Marks..."),                                   nullptr,               N_("Clear Marks"),                                     CB(layout_menu_clear_marks_cb) },
  { "CloseWindow",           GQ_ICON_CLOSE,                     N_("C_lose window"),                                    "<control>W",          N_("Close window"),                                    CB(layout_menu_close_cb) },
  { "ColorMenu",             nullptr,                           N_("_Color Management"),                                nullptr,               nullptr,                                               nullptr },
  { "ConnectZoom100Alt1",    GQ_ICON_ZOOM_100,                  N_("Zoom _1:1"),                                        "<shift>KP_Divide",    N_("Connected Zoom 1:1"),                              CB(layout_menu_connect_zoom_1_1_cb) },                 
  { "ConnectZoom100",        GQ_ICON_ZOOM_100,                  N_("Zoom _1:1"),                                        "<shift>Z",            N_("Connected Zoom 1:1"),                              CB(layout_menu_connect_zoom_1_1_cb) },
  { "ConnectZoom200",        nullptr,                           N_("Zoom _2:1"),                                        nullptr,               N_("Connected Zoom 2:1"),                              CB(layout_menu_connect_zoom_2_1_cb) },
  { "ConnectZoom25",         nullptr,                           N_("Zoom 1:4"),                                         nullptr,               N_("Connected Zoom 1:4"),                              CB(layout_menu_connect_zoom_1_4_cb) },
  { "ConnectZoom300",        nullptr,                           N_("Zoom _3:1"),                                        nullptr,               N_("Connected Zoom 3:1"),                              CB(layout_menu_connect_zoom_3_1_cb) },
  { "ConnectZoom33",         nullptr,                           N_("Zoom 1:3"),                                         nullptr,               N_("Connected Zoom 1:3"),                              CB(layout_menu_connect_zoom_1_3_cb) },
  { "ConnectZoom400",        nullptr,                           N_("Zoom _4:1"),                                        nullptr,               N_("Connected Zoom 4:1"),                              CB(layout_menu_connect_zoom_4_1_cb) },
  { "ConnectZoom50",         nullptr,                           N_("Zoom 1:2"),                                         nullptr,               N_("Connected Zoom 1:2"),                              CB(layout_menu_connect_zoom_1_2_cb) },
  { "ConnectZoomFillHor",    nullptr,                           N_("Fit _Horizontally"),                                "<shift>H",            N_("Connected Fit Horizontally"),                      CB(layout_menu_connect_zoom_fit_hor_cb) },             
  { "ConnectZoomFillVert",   nullptr,                           N_("Fit _Vertically"),                                  "<shift>W",            N_("Connected Fit Vertically"),                        CB(layout_menu_connect_zoom_fit_vert_cb) },            
  { "ConnectZoomFitAlt1",    GQ_ICON_ZOOM_FIT,                  N_("_Zoom to fit"),                                     "<shift>KP_Multiply",  N_("Connected Zoom to fit"),                           CB(layout_menu_connect_zoom_fit_cb) },                 
  { "ConnectZoomFit",        GQ_ICON_ZOOM_FIT,                  N_("_Zoom to fit"),                                     "<shift>X",            N_("Connected Zoom to fit"),                           CB(layout_menu_connect_zoom_fit_cb) },
  { "ConnectZoomInAlt1",     GQ_ICON_ZOOM_IN,                   N_("Zoom _in"),                                         "<shift>KP_Add",       N_("Connected Zoom in"),                               CB(layout_menu_connect_zoom_in_cb) },                  
  { "ConnectZoomIn",         GQ_ICON_ZOOM_IN,                   N_("Zoom _in"),                                         "plus",                N_("Connected Zoom in"),                               CB(layout_menu_connect_zoom_in_cb) },
  { "ConnectZoomMenu",       nullptr,                           N_("_Connected Zoom"),                                  nullptr,               nullptr,                                               nullptr },
  { "ConnectZoomOutAlt1",    GQ_ICON_ZOOM_OUT,                  N_("Zoom _out"),                                        "<shift>KP_Subtract",  N_("Connected Zoom out"),                              CB(layout_menu_connect_zoom_out_cb) },                 
  { "ConnectZoomOut",        GQ_ICON_ZOOM_OUT,                  N_("Zoom _out"),                                        "underscore",          N_("Connected Zoom out"),                              CB(layout_menu_connect_zoom_out_cb) },
  { "Copy",                  GQ_ICON_COPY,                      N_("_Copy..."),                                         "<control>C",          N_("Copy..."),                                         CB(layout_menu_copy_cb) },
  { "CopyImage",             nullptr,                           N_("_Copy image to clipboard"),                         nullptr,               N_("Copy image to clipboard"),                         CB(layout_menu_copy_image_cb) },
  { "CopyPath",              nullptr,                           N_("_Copy to clipboard"),                               nullptr,               N_("Copy to clipboard"),                               CB(layout_menu_copy_path_cb) },
  { "CopyPathUnquoted",      nullptr,                           N_("_Copy to clipboard (unquoted)"),                    nullptr,               N_("Copy to clipboard (unquoted)"),                    CB(layout_menu_copy_path_unquoted_cb) },
  { "CropRectangle",         nullptr,                           N_("Crop Rectangle"),                                   nullptr,               N_("Crop Rectangle"),                                  CB(layout_menu_crop_selection_cb) },
  { "CutPath",               nullptr,                           N_("_Cut to clipboard"),                                "<control>X",          N_("Cut to clipboard"),                                CB(layout_menu_cut_path_cb) },
  { "DeleteAlt1",            GQ_ICON_USER_TRASH,                N_("Move selection to Trash..."),                       "Delete",              N_("Move selection to Trash..."),                      CB(layout_menu_move_to_trash_key_cb) },
  { "DeleteAlt2",            GQ_ICON_USER_TRASH,                N_("Move selection to Trash..."),                       "KP_Delete",           N_("Move selection to Trash..."),                      CB(layout_menu_move_to_trash_key_cb) },
  { "Delete",                GQ_ICON_USER_TRASH,                N_("Move selection to Trash..."),                       "<control>D",          N_("Move selection to Trash..."),                      CB(layout_menu_move_to_trash_cb) },
  { "DeleteWindow",          GQ_ICON_DELETE,                    N_("Delete window"),                                    nullptr,               N_("Delete window"),                                   CB(layout_menu_window_delete_cb) },
  { "DisableGrouping",       nullptr,                           N_("Disable file groupi_ng"),                           nullptr,               N_("Disable file grouping"),                           CB(layout_menu_disable_grouping_cb) },
  { "EditMenu",              nullptr,                           N_("_Edit"),                                            nullptr,               nullptr,                                               nullptr },
  { "EnableGrouping",        nullptr,                           N_("Enable file _grouping"),                            nullptr,               N_("Enable file grouping"),                            CB(layout_menu_enable_grouping_cb) },
  { "EscapeAlt1",            GQ_ICON_LEAVE_FULLSCREEN,          N_("_Leave full screen"),                               "Q",                   N_("Leave full screen"),                               CB(layout_menu_escape_cb) },                           
  { "Escape",                GQ_ICON_LEAVE_FULLSCREEN,          N_("_Leave full screen"),                              "Escape",               N_("Leave full screen"),                               CB(layout_menu_escape_cb) },                           
  { "ExifWin",               PIXBUF_INLINE_ICON_EXIF,           N_("_Exif window"),                                     "<control>E",          N_("Exif window"),                                     CB(layout_menu_bar_exif_cb) },
  { "FileDirMenu",           nullptr,                           N_("_Files and Folders"),                               nullptr,               nullptr,                                               nullptr },
  { "FileMenu",              nullptr,                           N_("_File"),                                            nullptr,               nullptr,                                               nullptr },
  { "FindDupes",             GQ_ICON_FIND,                      N_("_Find duplicates..."),                              "D",                   N_("Find duplicates..."),                              CB(layout_menu_dupes_cb) },
  { "FirstImage",            GQ_ICON_GO_TOP,                    N_("_First Image"),                                     "Home",                N_("First Image"),                                     CB(layout_menu_image_first_cb) },
  { "FirstPage",             GQ_ICON_PREV_PAGE,                 N_("_First Page"),                                      "<control>Home",       N_( "First Page of multi-page image"),                 CB(layout_menu_page_first_cb) },
  { "Flip",                  GQ_ICON_FLIP_VERTICAL,             N_("_Flip"),                                            "<shift>F",            N_("Image Flip"),                                      CB(layout_menu_alter_flip_cb) },
  { "Forward",               GQ_ICON_GO_NEXT,                   N_("_Forward"),                                         nullptr,               N_("Forward in folder history"),                       CB(layout_menu_forward_cb) },
  { "FullScreenAlt1",        GQ_ICON_FULLSCREEN,                N_("F_ull screen"),                                     "V",                   N_("Full screen"),                                     CB(layout_menu_fullscreen_cb) },
  { "FullScreenAlt2",        GQ_ICON_FULLSCREEN,                N_("F_ull screen"),                                     "F11",                 N_("Full screen"),                                     CB(layout_menu_fullscreen_cb) },
  { "FullScreen",            GQ_ICON_FULLSCREEN,                N_("F_ull screen"),                                     "F",                   N_("Full screen"),                                     CB(layout_menu_fullscreen_cb) },
  { "GoMenu",                nullptr,                           N_("_Go"),                                              nullptr,               nullptr,                                               nullptr },
  { "HelpChangeLog",         nullptr,                           N_("_ChangeLog"),                                       nullptr,               N_("ChangeLog notes"),                                 CB(layout_menu_changelog_cb) },
  { "HelpContents",          GQ_ICON_HELP,                      N_("_Help manual"),                                     "F1",                  N_("Help manual"),                                     CB(layout_menu_help_cb) },
  { "HelpKbd",               nullptr,                           N_("_Keyboard map"),                                    nullptr,               N_("Keyboard map"),                                    CB(layout_menu_kbd_map_cb) },
  { "HelpMenu",              nullptr,                           N_("_Help"),                                            nullptr,               nullptr,                                               nullptr },
  { "HelpNotes",             nullptr,                           N_("_Readme"),                                          nullptr,               N_("Readme"),                                          CB(layout_menu_notes_cb) },
  { "HelpPdf",               nullptr,                           N_("Help in pdf format"),                               nullptr,               N_("Help in pdf formast"),                             CB(layout_menu_help_pdf_cb) },
  { "HelpSearch",            nullptr,                           N_("On-line help search"),                              nullptr,               N_("On-line help search"),                             CB(layout_menu_help_search_cb) },
  { "HelpShortcuts",         nullptr,                           N_("_Keyboard shortcuts"),                              nullptr,               N_("Keyboard shortcuts"),                              CB(layout_menu_help_keys_cb) },
  { "HideTools",             PIXBUF_INLINE_ICON_HIDETOOLS,      N_("_Hide file list"),                                  "<control>H",          N_("Hide file list"),                                  CB(layout_menu_hide_cb) },
  { "HistogramChanCycle",    nullptr,                           N_("Cycle through histogram ch_annels"),                "K",                   N_("Cycle through histogram channels"),                CB(layout_menu_histogram_toggle_channel_cb) },                                                         
  { "HistogramModeCycle",    nullptr,                           N_("Cycle through histogram mo_des"),                   "J",                   N_("Cycle through histogram modes"),                   CB(layout_menu_histogram_toggle_mode_cb) },            
  { "Home",                  GQ_ICON_HOME,                      N_("_Home"),                                            nullptr,               N_("Home"),                                            CB(layout_menu_home_cb) },
  { "ImageBack",             GQ_ICON_GO_FIRST,                  N_("Image Back"),                                       nullptr,               N_("Back in image history"),                           CB(layout_menu_image_back_cb) },
  { "ImageForward",          GQ_ICON_GO_LAST,                   N_("Image Forward"),                                    nullptr,               N_("Forward in image history"),                        CB(layout_menu_image_forward_cb) },
  { "ImageOverlayCycle",     nullptr,                           N_("_Cycle through overlay modes"),                     "I",                   N_("Cycle through Overlay modes"),                     CB(layout_menu_overlay_toggle_cb) },                   
  { "KeywordAutocomplete",   nullptr,                           N_("Keyword autocomplete"),                             "<alt>K",              N_("Keyword Autocomplete"),                            CB(layout_menu_keyword_autocomplete_cb) },
  { "LastImage",             GQ_ICON_GO_BOTTOM,                 N_("_Last Image"),                                      "End",                 N_("Last Image"),                                      CB(layout_menu_image_last_cb) },
  { "LastPage",              GQ_ICON_NEXT_PAGE,                 N_("_Last Page"),                                       "<control>End",        N_("Last Page of multi-page image"),                   CB(layout_menu_page_last_cb) },
  { "LayoutConfig",          GQ_ICON_PREFERENCES,               N_("_Configure this window..."),                        nullptr,               N_("Configure this window..."),                        CB(layout_menu_layout_config_cb) },
  { "LogWindow",             nullptr,                           N_("_Log Window"),                                      nullptr,               N_("Log Window"),                                      CB(layout_menu_log_window_cb) },
  { "Maintenance",           PIXBUF_INLINE_ICON_MAINTENANCE,    N_("_Cache maintenance..."),                            nullptr,               N_("Cache maintenance..."),                            CB(layout_menu_remove_thumb_cb) },
  { "Mirror",                GQ_ICON_FLIP_HORIZONTAL,           N_("_Mirror"),                                          "<shift>M",            N_("Image Mirror"),                                    CB(layout_menu_alter_mirror_cb) },
  { "Move",                  PIXBUF_INLINE_ICON_MOVE,           N_("_Move..."),                                         "<control>M",          N_("Move..."),                                         CB(layout_menu_move_cb) },
  { "NewCollection",         PIXBUF_INLINE_COLLECTION,          N_("_New collection"),                                  "C",                   N_("New collection"),                                  CB(layout_menu_new_cb) },
  { "NewFolder",             GQ_ICON_DIRECTORY,                 N_("N_ew folder..."),                                   "<control>F",          N_("New folder..."),                                   CB(layout_menu_dir_cb) },
  { "NewWindowDefault",      nullptr,                           N_("default"),                                          "<control>N",          N_("New window (default)"),                            CB(layout_menu_window_default_cb)  },
  { "NewWindowFromCurrent",  nullptr,                           N_("from current"),                                     nullptr,               N_("from current"),                                    CB(layout_menu_window_from_current_cb)  },
  { "NewWindow",             nullptr,                           N_("New window"),                                       nullptr,               N_("New window"),                                      CB(layout_menu_window_cb) },
  { "NextImageAlt1",         GQ_ICON_GO_DOWN,                   N_("_Next Image"),                                      "Page_Down",           N_("Next Image"),                                      CB(layout_menu_image_next_cb) },
  { "NextImageAlt2",         GQ_ICON_GO_DOWN,                   N_("_Next Image"),                                      "KP_Page_Down",        N_("Next Image"),                                      CB(layout_menu_image_next_cb) },
  { "NextImage",             GQ_ICON_GO_DOWN,                   N_("_Next Image"),                                      "space",               N_("Next Image"),                                      CB(layout_menu_image_next_cb) },
  { "NextPage",              GQ_ICON_FORWARD_PAGE,              N_("_Next Page"),                                       "<control>Page_Down",  N_("Next Page of multi-page image"),                   CB(layout_menu_page_next_cb) },
  { "OpenArchive",           GQ_ICON_OPEN,                      N_("Open archive"),                                     nullptr,               N_("Open archive"),                                    CB(layout_menu_open_archive_cb) },
  { "OpenCollection",        GQ_ICON_OPEN,                      N_("_Open collection..."),                              "O",                   N_("Open collection..."),                              nullptr },
  { "OpenMenu",              nullptr,                           N_("☰"),                                                nullptr,               nullptr,                                               nullptr },
  { "OpenRecent",            nullptr,                           N_("Open recen_t"),                                     nullptr,               N_("Open recent collection"),                          nullptr },
  { "OpenWith",              GQ_ICON_OPEN_WITH,                 N_("Open With..."),                                     nullptr,               N_("Open With..."),                                    CB(layout_menu_open_with_cb) },
  { "OrientationMenu",       nullptr,                           N_("_Orientation"),                                     nullptr,               nullptr,                                               nullptr },
  { "OverlayMenu",           nullptr,                           N_("Image _Overlay"),                                   nullptr,               nullptr,                                               nullptr },
  { "PanView",               PIXBUF_INLINE_ICON_PANORAMA,       N_("Pa_n view"),                                        "<control>J",          N_("Pan view"),                                        CB(layout_menu_pan_cb) },
  { "PermanentDelete",       GQ_ICON_DELETE,                    N_("Delete selection..."),                              "<shift>Delete",       N_("Delete selection..."),                             CB(layout_menu_delete_cb) },
  { "Plugins",               GQ_ICON_PREFERENCES,               N_("Configure _Plugins..."),                            nullptr,               N_("Configure Plugins..."),                            CB(layout_menu_editors_cb) },
  { "PluginsMenu",           nullptr,                           N_("_Plugins"),                                         nullptr,               nullptr,                                               nullptr },
  { "Preferences",           GQ_ICON_PREFERENCES,               N_("P_references..."),                                  "<control>O",          N_("Preferences..."),                                  CB(layout_menu_config_cb) },
  { "PreferencesMenu",       nullptr,                           N_("P_references"),                                     nullptr,               nullptr,                                               nullptr },
  { "PrevImageAlt1",         GQ_ICON_GO_UP,                     N_("_Previous Image"),                                  "Page_Up",             N_("Previous Image"),                                  CB(layout_menu_image_prev_cb) },
  { "PrevImageAlt2",         GQ_ICON_GO_UP,                     N_("_Previous Image"),                                  "KP_Page_Up",          N_("Previous Image"),                                  CB(layout_menu_image_prev_cb) },
  { "PrevImage",             GQ_ICON_GO_UP,                     N_("_Previous Image"),                                  "BackSpace",           N_("Previous Image"),                                  CB(layout_menu_image_prev_cb) },
  { "PrevPage",              GQ_ICON_BACK_PAGE,                 N_("_Previous Page"),                                   "<control>Page_Up",    N_("Previous Page of multi-page image"),               CB(layout_menu_page_previous_cb) },
  { "Print",                 GQ_ICON_PRINT,                     N_("_Print..."),                                        "<shift>P",            N_("Print..."),                                        CB(layout_menu_print_cb) },
  { "Quit",                  GQ_ICON_QUIT,                      N_("_Quit"),                                            "<control>Q",          N_("Quit"),                                            CB(layout_menu_exit_cb) },
  { "Rating0",               nullptr,                           N_("_Rating 0"),                                        "<alt>KP_0",           N_("Rating 0"),                                        CB(layout_menu_rating_0_cb) },
  { "Rating1",               nullptr,                           N_("_Rating 1"),                                        "<alt>KP_1",           N_("Rating 1"),                                        CB(layout_menu_rating_1_cb) },
  { "Rating2",               nullptr,                           N_("_Rating 2"),                                        "<alt>KP_2",           N_("Rating 2"),                                        CB(layout_menu_rating_2_cb) },
  { "Rating3",               nullptr,                           N_("_Rating 3"),                                        "<alt>KP_3",           N_("Rating 3"),                                        CB(layout_menu_rating_3_cb) },
  { "Rating4",               nullptr,                           N_("_Rating 4"),                                        "<alt>KP_4",           N_("Rating 4"),                                        CB(layout_menu_rating_4_cb) },
  { "Rating5",               nullptr,                           N_("_Rating 5"),                                        "<alt>KP_5",           N_("Rating 5"),                                        CB(layout_menu_rating_5_cb) },
  { "RatingM1",              nullptr,                           N_("_Rating -1"),                                       "<alt>KP_Subtract",    N_("Rating -1"),                                       CB(layout_menu_rating_m1_cb) },
  { "RatingMenu",            nullptr,                           N_("_Rating"),                                          nullptr,               nullptr,                                               nullptr },
  { "Refresh",               GQ_ICON_REFRESH,                   N_("_Refresh"),                                         "R",                   N_("Refresh"),                                         CB(layout_menu_refresh_cb) },
  { "Rename",                PIXBUF_INLINE_ICON_RENAME,         N_("_Rename..."),                                       "<control>R",          N_("Rename..."),                                       CB(layout_menu_rename_cb) },
  { "RenameWindow",          GQ_ICON_EDIT,                      N_("Rename window"),                                    nullptr,               N_("Rename window"),                                   CB(layout_menu_window_rename_cb) },
  { "Rotate180",             PIXBUF_INLINE_ICON_180,            N_("Rotate 1_80°"),                                     "<shift>R",            N_("Image Rotate 180°"),                               CB(layout_menu_alter_180_cb) },
  { "RotateCCW",             GQ_ICON_ROTATE_LEFT,               N_("Rotate _counterclockwise 90°"),                     "bracketleft",         N_("Rotate counterclockwise 90°"),                     CB(layout_menu_alter_90cc_cb) },
  { "RotateCW",              GQ_ICON_ROTATE_RIGHT,              N_("_Rotate clockwise 90°"),                            "bracketright",        N_("Image Rotate clockwise 90°"),                      CB(layout_menu_alter_90_cb) },
  { "SaveMetadata",          GQ_ICON_SAVE,                      N_("_Save metadata"),                                   "<control>S",          N_("Save metadata"),                                   CB(layout_menu_metadata_write_cb) },
  { "SearchAndRunCommand",   GQ_ICON_FIND,                      N_("Search and Run command"),                           "slash",               N_("Search commands by keyword and run them"),         CB(layout_menu_search_and_run_cb) },
  { "Search",                GQ_ICON_FIND,                      N_("_Search..."),                                       "F3",                  N_("Search..."),                                       CB(layout_menu_search_cb) },
  { "SelectAll",             PIXBUF_INLINE_ICON_SELECT_ALL,     N_("Select _all"),                                      "<control>A",          N_("Select all"),                                      CB(layout_menu_select_all_cb) },
  { "SelectInvert",          PIXBUF_INLINE_ICON_SELECT_INVERT,  N_("_Invert Selection"),                                "<control><shift>I",   N_("Invert Selection"),                                CB(layout_menu_invert_selection_cb) },
  { "SelectMenu",            nullptr,                           N_("_Select"),                                          nullptr,               nullptr,                                               nullptr },
  { "SelectNone",            PIXBUF_INLINE_ICON_SELECT_NONE,    N_("Select _none"),                                     "<control><shift>A",   N_("Select none"),                                     CB(layout_menu_unselect_all_cb) },
  { "SlideShowFaster",       GQ_ICON_GENERIC,                   N_("Faster"),                                           "<control>equal",      N_("Slideshow Faster"),                                CB(layout_menu_slideshow_faster_cb) },
  { "SlideShowPause",        GQ_ICON_PAUSE,                     N_("_Pause slideshow"),                                 "P",                   N_("Pause slideshow"),                                 CB(layout_menu_slideshow_pause_cb) },
  { "SlideShowSlower",       GQ_ICON_GENERIC,                   N_("Slower"),                                           "<control>minus",      N_("Slideshow Slower"),                                CB(layout_menu_slideshow_slower_cb) },
  { "SplitDownPane",         nullptr,                           N_("_Down Pane"),                                       "<alt>Down",           N_("Down Split Pane"),                                 CB(layout_menu_split_pane_updown_cb) },
  { "SplitMenu",             nullptr,                           N_("Spli_t"),                                           nullptr,               nullptr,                                               nullptr },
  { "SplitNextPane",         nullptr,                           N_("_Next Pane"),                                       "<alt>Right",          N_("Next Split Pane"),                                 CB(layout_menu_split_pane_next_cb) },
  { "SplitPreviousPane",     nullptr,                           N_("_Previous Pane"),                                   "<alt>Left",           N_("Previous Split Pane"),                             CB(layout_menu_split_pane_prev_cb) },
  { "SplitUpPane",           nullptr,                           N_("_Up Pane"),                                         "<alt>Up",             N_("Up Split Pane"),                                   CB(layout_menu_split_pane_updown_cb) },
  { "StereoCycle",           nullptr,                           N_("_Cycle through stereo modes"),                      nullptr,               N_("Cycle through stereo modes"),                      CB(layout_menu_stereo_mode_next_cb) },
  { "StereoMenu",            nullptr,                           N_("Stere_o"),                                          nullptr,               nullptr,                                               nullptr },
  { "Up",                    GQ_ICON_GO_UP,                     N_("_Up"),                                              nullptr,               N_("Up one folder"),                                   CB(layout_menu_up_cb) },
  { "ViewInNewWindow",       nullptr,                           N_("_View in new window"),                              "<control>V",          N_("View in new window"),                              CB(layout_menu_view_in_new_window_cb) },
  { "ViewMenu",              nullptr,                           N_("_View"),                                            nullptr,               nullptr,                                               CB(layout_menu_view_menu_cb)  },
  { "Wallpaper",             nullptr,                           N_("Set as _wallpaper"),                                nullptr,               N_("Set as wallpaper"),                                CB(layout_menu_wallpaper_cb) },
  { "WindowsMenu",           nullptr,                           N_("_Windows"),                                         nullptr,               nullptr,                                               CB(layout_menu_windows_menu_cb)  },
  { "WriteRotationKeepDate", nullptr,                           N_("_Write orientation to file (preserve timestamp)"),  nullptr,               N_("Write orientation to file (preserve timestamp)"),  CB(layout_menu_write_rotate_keep_date_cb) },
  { "WriteRotation",         nullptr,                           N_("_Write orientation to file"),                       nullptr,               N_("Write orientation to file"),                       CB(layout_menu_write_rotate_cb) },
  { "Zoom100Alt1",           GQ_ICON_ZOOM_100,                  N_("Zoom _1:1"),                                        "KP_Divide",           N_("Zoom 1:1"),                                        CB(layout_menu_zoom_1_1_cb) },
  { "Zoom100",               GQ_ICON_ZOOM_100,                  N_("Zoom _1:1"),                                        "Z",                   N_("Zoom 1:1"),                                        CB(layout_menu_zoom_1_1_cb) },
  { "Zoom200",               GQ_ICON_GENERIC,                   N_("Zoom _2:1"),                                        nullptr,               N_("Zoom 2:1"),                                        CB(layout_menu_zoom_2_1_cb) },
  { "Zoom25",                GQ_ICON_GENERIC,                   N_("Zoom 1:4"),                                         nullptr,               N_("Zoom 1:4"),                                        CB(layout_menu_zoom_1_4_cb) },
  { "Zoom300",               GQ_ICON_GENERIC,                   N_("Zoom _3:1"),                                        nullptr,               N_("Zoom 3:1"),                                        CB(layout_menu_zoom_3_1_cb) },
  { "Zoom33",                GQ_ICON_GENERIC,                   N_("Zoom 1:3"),                                         nullptr,               N_("Zoom 1:3"),                                        CB(layout_menu_zoom_1_3_cb) },
  { "Zoom400",               GQ_ICON_GENERIC,                   N_("Zoom _4:1"),                                        nullptr,               N_("Zoom 4:1"),                                        CB(layout_menu_zoom_4_1_cb) },
  { "Zoom50",                GQ_ICON_GENERIC,                   N_("Zoom 1:2"),                                         nullptr,               N_("Zoom 1:2"),                                        CB(layout_menu_zoom_1_2_cb) },
  { "ZoomFillHor",           PIXBUF_INLINE_ICON_ZOOMFILLHOR,    N_("Fit _Horizontally"),                                "H",                   N_("Fit Horizontally"),                                CB(layout_menu_zoom_fit_hor_cb) },
  { "ZoomFillVert",          PIXBUF_INLINE_ICON_ZOOMFILLVERT,   N_("Fit _Vertically"),                                  "W",                   N_("Fit Vertically"),                                  CB(layout_menu_zoom_fit_vert_cb) },
  { "ZoomFitAlt1",           GQ_ICON_ZOOM_FIT,                  N_("_Zoom to fit"),                                     "KP_Multiply",         N_("Zoom to fit"),                                     CB(layout_menu_zoom_fit_cb) },
  { "ZoomFit",               GQ_ICON_ZOOM_FIT,                  N_("_Zoom to fit"),                                     "X",                   N_("Zoom to fit"),                                     CB(layout_menu_zoom_fit_cb) },
  { "ZoomInAlt1",            GQ_ICON_ZOOM_IN,                   N_("Zoom _in"),                                         "KP_Add",              N_("Zoom in"),                                         CB(layout_menu_zoom_in_cb) },
  { "ZoomIn",                GQ_ICON_ZOOM_IN,                   N_("Zoom _in"),                                         "equal",               N_("Zoom in"),                                         CB(layout_menu_zoom_in_cb) },
  { "ZoomMenu",              nullptr,                           N_("_Zoom"),                                            nullptr,               nullptr,                                               nullptr },
  { "ZoomOutAlt1",           GQ_ICON_ZOOM_OUT,                  N_("Zoom _out"),                                        "KP_Subtract",         N_("Zoom out"),                                        CB(layout_menu_zoom_out_cb) },
  { "ZoomToRectangle",       nullptr,                           N_("Zoom to rectangle"),                                nullptr,               N_("Zoom to rectangle"),                               CB(layout_menu_zoom_to_rectangle_cb) },
  { "ZoomOut",               GQ_ICON_ZOOM_OUT,                  N_("Zoom _out"),                                        "minus",               N_("Zoom out"),                                        CB(layout_menu_zoom_out_cb) }
};

static GtkToggleActionEntry menu_toggle_entries[] = {
  { "Animate",                 nullptr,                              N_("_Animation"),               "A",               N_("Toggle animation"),              CB(layout_menu_animate_cb),                  FALSE  },
  { "DrawRectangle",           PIXBUF_INLINE_ICON_DRAW_RECTANGLE,    N_("Draw Rectangle"),           nullptr,           N_("Draw Rectangle"),                CB(layout_menu_select_rectangle_cb),         FALSE  },
  { "ExifRotate",              GQ_ICON_ROTATE_LEFT,                  N_("_Exif rotate"),             "<alt>X",          N_("Toggle Exif rotate"),            CB(layout_menu_exif_rotate_cb),              FALSE  },
  { "FloatTools",              PIXBUF_INLINE_ICON_FLOAT,             N_("_Float file list"),         "L",               N_("Float file list"),               CB(layout_menu_float_cb),                    FALSE  },
  { "Grayscale",               PIXBUF_INLINE_ICON_GRAYSCALE,         N_("Toggle _grayscale"),        "<shift>G",        N_("Toggle grayscale"),              CB(layout_menu_alter_desaturate_cb),         FALSE  },
  { "HideBars",                nullptr,                              N_("Hide Bars and Files"),      "grave",           N_("Hide Bars and Files"),           CB(layout_menu_hide_bars_cb),                FALSE  },
  { "HideSelectableToolbars",  nullptr,                              N_("Hide Selectable Bars"),     "<control>grave",  N_("Hide Selectable Bars"),          CB(layout_menu_selectable_toolbars_cb),      FALSE  },
  { "IgnoreAlpha",             GQ_ICON_STRIKETHROUGH,                N_("Hide _alpha"),              "<shift>A",        N_("Hide alpha channel"),            CB(layout_menu_alter_ignore_alpha_cb),       FALSE  },
  { "ImageHistogram",          nullptr,                              N_("_Show Histogram"),          nullptr,           N_("Show Histogram"),                CB(layout_menu_histogram_cb),                FALSE  },
  { "ImageOverlay",            nullptr,                              N_("Image _Overlay"),           nullptr,           N_("Image Overlay"),                 CB(layout_menu_overlay_cb),                  FALSE  },
  { "OverUnderExposed",        PIXBUF_INLINE_ICON_EXPOSURE,          N_("Over/Under Exposed"),       "<shift>E",        N_("Highlight over/under exposed"),  CB(layout_menu_select_overunderexposed_cb),  FALSE  },
  { "RectangularSelection",    PIXBUF_INLINE_ICON_SELECT_RECTANGLE,  N_("Rectangular Selection"),    "<alt>R",          N_("Rectangular Selection"),         CB(layout_menu_rectangular_selection_cb),    FALSE  },
  { "SBar",                    PIXBUF_INLINE_ICON_PROPERTIES,        N_("_Info sidebar"),            "<control>K",      N_("Info sidebar"),                  CB(layout_menu_bar_cb),                      FALSE  },
  { "SBarSort",                PIXBUF_INLINE_ICON_SORT,              N_("Sort _manager"),            "<shift>S",        N_("Sort manager"),                  CB(layout_menu_bar_sort_cb),                 FALSE  },
  { "ShowFileFilter",          GQ_ICON_FILE_FILTER,                  N_("Show File Filter"),         nullptr,           N_("Show File Filter"),              CB(layout_menu_file_filter_cb),              FALSE  },
  { "ShowInfoPixel",           GQ_ICON_SELECT_COLOR,                 N_("Pi_xel Info"),              nullptr,           N_("Show Pixel Info"),               CB(layout_menu_info_pixel_cb),               FALSE  },
  { "ShowMarks",               PIXBUF_INLINE_ICON_MARKS,             N_("Show _Marks"),              "M",               N_("Show Marks"),                    CB(layout_menu_marks_cb),                    FALSE  },
  { "SlideShow",               GQ_ICON_PLAY,                         N_("Toggle _slideshow"),        "S",               N_("Toggle slideshow"),              CB(layout_menu_slideshow_cb),                FALSE  },
  { "SplitPaneSync",           PIXBUF_INLINE_SPLIT_PANE_SYNC,        N_("Split Pane Sync"),          nullptr,           N_("Split Pane Sync"),               CB(layout_menu_split_pane_sync_cb),          FALSE  },
  { "Thumbnails",              PIXBUF_INLINE_ICON_THUMB,             N_("Show _Thumbnails"),         "T",               N_("Show Thumbnails"),               CB(layout_menu_thumb_cb),                    FALSE  },
  { "UseColorProfiles",        GQ_ICON_COLOR_MANAGEMENT,             N_("Use _color profiles"),      nullptr,           N_("Use color profiles"),            CB(layout_color_menu_enable_cb),             FALSE  },
  { "UseImageProfile",         nullptr,                              N_("Use profile from _image"),  nullptr,           N_("Use profile from image"),        CB(layout_color_menu_use_image_cb),          FALSE  }
};

static GtkRadioActionEntry menu_radio_entries[] = {
  { "ViewIcons",  nullptr,  N_("Images as I_cons"),  "<control>I",  N_("View Images as Icons"),  FILEVIEW_ICON },
  { "ViewList",   nullptr,  N_("Images as _List"),   "<control>L",  N_("View Images as List"),   FILEVIEW_LIST }
};

static GtkToggleActionEntry menu_view_dir_toggle_entries[] = {
  { "FolderTree",  nullptr,  N_("T_oggle Folder View"),  "<control>T",  N_("Toggle Folders View"),  CB(layout_menu_view_dir_as_cb),FALSE },
};

static GtkRadioActionEntry menu_split_radio_entries[] = {
  { "SplitHorizontal",  nullptr,  N_("_Horizontal"),  "E",      N_("Split panes horizontal."),  SPLIT_HOR },
  { "SplitQuad",        nullptr,  N_("_Quad"),        nullptr,  N_("Split panes quad"),         SPLIT_QUAD },
  { "SplitSingle",      nullptr,  N_("_Single"),      "Y",      N_("Single pane"),              SPLIT_NONE },
  { "SplitTriple",      nullptr,  N_("_Triple"),      nullptr,  N_("Split panes triple"),       SPLIT_TRIPLE },
  { "SplitVertical",    nullptr,  N_("_Vertical"),    "U",      N_("Split panes vertical"),     SPLIT_VERT }
};

static GtkRadioActionEntry menu_color_radio_entries[] = {
  { "ColorProfile0",  nullptr,  N_("Input _0: sRGB"),                 nullptr,  N_("Input 0: sRGB"),                 COLOR_PROFILE_SRGB },
  { "ColorProfile1",  nullptr,  N_("Input _1: AdobeRGB compatible"),  nullptr,  N_("Input 1: AdobeRGB compatible"),  COLOR_PROFILE_ADOBERGB },
  { "ColorProfile2",  nullptr,  N_("Input _2"),                       nullptr,  N_("Input 2"),                       COLOR_PROFILE_FILE },
  { "ColorProfile3",  nullptr,  N_("Input _3"),                       nullptr,  N_("Input 3"),                       COLOR_PROFILE_FILE + 1 },
  { "ColorProfile4",  nullptr,  N_("Input _4"),                       nullptr,  N_("Input 4"),                       COLOR_PROFILE_FILE + 2 },
  { "ColorProfile5",  nullptr,  N_("Input _5"),                       nullptr,  N_("Input 5"),                       COLOR_PROFILE_FILE + 3 }
};

static GtkRadioActionEntry menu_histogram_channel[] = {
  { "HistogramChanB",    nullptr,  N_("Histogram on _Blue"),   nullptr,  N_("Histogram on Blue"),   HCHAN_B },
  { "HistogramChanG",    nullptr,  N_("Histogram on _Green"),  nullptr,  N_("Histogram on Green"),  HCHAN_G },
  { "HistogramChanRGB",  nullptr,  N_("_Histogram on RGB"),    nullptr,  N_("Histogram on RGB"),    HCHAN_RGB },
  { "HistogramChanR",    nullptr,  N_("Histogram on _Red"),    nullptr,  N_("Histogram on Red"),    HCHAN_R },
  { "HistogramChanV",    nullptr,  N_("Histogram on _Value"),  nullptr,  N_("Histogram on Value"),  HCHAN_MAX }
};

static GtkRadioActionEntry menu_histogram_mode[] = {
  { "HistogramModeLin",  nullptr,  N_("Li_near Histogram"),  nullptr,  N_("Linear Histogram"),  0 },
  { "HistogramModeLog",  nullptr,  N_("_Log Histogram"),     nullptr,  N_("Log Histogram"),     1 },
};

static GtkRadioActionEntry menu_stereo_mode_entries[] = {
  { "StereoAuto",   nullptr,  N_("_Auto"),          nullptr,  N_("Stereo Auto"),          STEREO_PIXBUF_DEFAULT },
  { "StereoCross",  nullptr,  N_("_Cross"),         nullptr,  N_("Stereo Cross"),         STEREO_PIXBUF_CROSS },
  { "StereoOff",    nullptr,  N_("_Off"),           nullptr,  N_("Stereo Off"),           STEREO_PIXBUF_NONE },
  { "StereoSBS",    nullptr,  N_("_Side by Side"),  nullptr,  N_("Stereo Side by Side"),  STEREO_PIXBUF_SBS }
};

static GtkRadioActionEntry menu_draw_rectangle_aspect_ratios[] = {
  { "CropNone",        nullptr, N_("Crop None"), nullptr, N_("Crop rectangle None"), RECTANGLE_DRAW_ASPECT_RATIO_NONE },
  { "CropOneOne",      nullptr, N_("Crop 1:1"),  nullptr, N_("Crop rectangle 1:1"),  RECTANGLE_DRAW_ASPECT_RATIO_ONE_ONE },
  { "CropFourThree",   nullptr, N_("Crop 4:3"),  nullptr, N_("Crop rectangle 4:3"),  RECTANGLE_DRAW_ASPECT_RATIO_FOUR_THREE },
  { "CropThreeTwo",    nullptr, N_("Crop 3:2"),  nullptr, N_("Crop rectangle 3:2"),  RECTANGLE_DRAW_ASPECT_RATIO_THREE_TWO },
  { "CropSixteenNine", nullptr, N_("Crop 16:9"), nullptr, N_("Crop rectangle 16:9"), RECTANGLE_DRAW_ASPECT_RATIO_SIXTEEN_NINE }
};
#undef CB

static gchar *menu_translate(const gchar *path, gpointer)
{
	return static_cast<gchar *>(_(path));
}

static void layout_actions_setup_mark(LayoutWindow *lw, gint mark, const gchar *name_tmpl,
				      const gchar *label_tmpl, const gchar *accel_tmpl, const gchar *tooltip_tmpl, GCallback cb)
{
	gchar name[50];
	gchar label[100];
	gchar accel[50];
	gchar tooltip[100];
	GtkActionEntry entry = { name, nullptr, label, accel, tooltip, cb };
	GtkAction *action;

	g_snprintf(name, sizeof(name), name_tmpl, mark);
	g_snprintf(label, sizeof(label), label_tmpl, mark);

	if (accel_tmpl)
		g_snprintf(accel, sizeof(accel), accel_tmpl, mark % 10);
	else
		entry.accelerator = nullptr;

	if (tooltip_tmpl)
		g_snprintf(tooltip, sizeof(tooltip), tooltip_tmpl, mark);
	else
		entry.tooltip = nullptr;

	gq_gtk_action_group_add_actions(lw->action_group, &entry, 1, lw);
	action = gq_gtk_action_group_get_action(lw->action_group, name);
	g_object_set_data(G_OBJECT(action), "mark_num", GINT_TO_POINTER(mark > 0 ? mark : 10));
}

static void layout_actions_setup_marks(LayoutWindow *lw)
{
	gint mark;
	GError *error;
	GString *desc = g_string_new(
				"<ui>"
				"  <menubar name='MainMenu'>");

	if (options->hamburger_menu)
		{
		g_string_append(desc, "    <menu action='OpenMenu'>");
		}
	g_string_append(desc, "      <menu action='SelectMenu'>");

	for (mark = 1; mark <= FILEDATA_MARKS_SIZE; mark++)
		{
		gint i = (mark < 10 ? mark : 0);

		layout_actions_setup_mark(lw, i, "Mark%d",		_("Mark _%d"), nullptr, nullptr, nullptr);
		layout_actions_setup_mark(lw, i, "SetMark%d",	_("_Set mark %d"),			nullptr,		_("Set mark %d"), G_CALLBACK(layout_menu_set_mark_sel_cb));
		layout_actions_setup_mark(lw, i, "ResetMark%d",	_("_Reset mark %d"),			nullptr,		_("Reset mark %d"), G_CALLBACK(layout_menu_res_mark_sel_cb));
		layout_actions_setup_mark(lw, i, "ToggleMark%d",	_("_Toggle mark %d"),			"%d",		_("Toggle mark %d"), G_CALLBACK(layout_menu_toggle_mark_sel_cb));
		layout_actions_setup_mark(lw, i, "ToggleMark%dAlt1",	_("_Toggle mark %d"),			"KP_%d",	_("Toggle mark %d"), G_CALLBACK(layout_menu_toggle_mark_sel_cb));
		layout_actions_setup_mark(lw, i, "SelectMark%d",	_("Se_lect mark %d"),			"<control>%d",	_("Select mark %d"), G_CALLBACK(layout_menu_sel_mark_cb));
		layout_actions_setup_mark(lw, i, "SelectMark%dAlt1",	_("_Select mark %d"),			"<control>KP_%d", _("Select mark %d"), G_CALLBACK(layout_menu_sel_mark_cb));
		layout_actions_setup_mark(lw, i, "AddMark%d",	_("_Add mark %d"),			nullptr,		_("Add mark %d"), G_CALLBACK(layout_menu_sel_mark_or_cb));
		layout_actions_setup_mark(lw, i, "IntMark%d",	_("_Intersection with mark %d"),	nullptr,		_("Intersection with mark %d"), G_CALLBACK(layout_menu_sel_mark_and_cb));
		layout_actions_setup_mark(lw, i, "UnselMark%d",	_("_Unselect mark %d"),			nullptr,		_("Unselect mark %d"), G_CALLBACK(layout_menu_sel_mark_minus_cb));
		layout_actions_setup_mark(lw, i, "FilterMark%d",	_("_Filter mark %d"),			nullptr,		_("Filter mark %d"), G_CALLBACK(layout_menu_mark_filter_toggle_cb));

		g_string_append_printf(desc,
				"      <menu action='Mark%d'>"
				"        <menuitem action='ToggleMark%d'/>"
				"        <menuitem action='SetMark%d'/>"
				"        <menuitem action='ResetMark%d'/>"
				"        <separator/>"
				"        <menuitem action='SelectMark%d'/>"
				"        <menuitem action='AddMark%d'/>"
				"        <menuitem action='IntMark%d'/>"
				"        <menuitem action='UnselMark%d'/>"
				"        <separator/>"
				"        <menuitem action='FilterMark%d'/>"
				"      </menu>",
				i, i, i, i, i, i, i, i, i);
		}

	g_string_append(desc,
				"      </menu>");
	if (options->hamburger_menu)
		{
		g_string_append(desc, "    </menu>");
		}
	g_string_append(desc, "  </menubar>");

	for (mark = 1; mark <= FILEDATA_MARKS_SIZE; mark++)
		{
		gint i = (mark < 10 ? mark : 0);

		g_string_append_printf(desc,
				"<accelerator action='ToggleMark%dAlt1'/>"
				"<accelerator action='SelectMark%dAlt1'/>",
				i, i);
		}
	g_string_append(desc,   "</ui>" );

	error = nullptr;
	if (!gq_gtk_ui_manager_add_ui_from_string(lw->ui_manager, desc->str, -1, &error))
		{
		g_message("building menus failed: %s", error->message);
		g_error_free(error);
		exit(EXIT_FAILURE);
		}
	g_string_free(desc, TRUE);
}

static GList *layout_actions_editor_menu_path(EditorDescription *editor)
{
	gchar **split = g_strsplit(editor->menu_path, "/", 0);
	gint i = 0;
	GList *ret = nullptr;

	if (split[0] == nullptr)
		{
		g_strfreev(split);
		return nullptr;
		}

	while (split[i])
		{
		ret = g_list_prepend(ret, g_strdup(split[i]));
		i++;
		}

	g_strfreev(split);

	ret = g_list_prepend(ret, g_strdup(editor->key));

	return g_list_reverse(ret);
}

static void layout_actions_editor_add(GString *desc, GList *path, GList *old_path)
{
	gint to_open;
	gint to_close;
	gint i;
	while (path && old_path && strcmp(static_cast<gchar *>(path->data), static_cast<gchar *>(old_path->data)) == 0)
		{
		path = path->next;
		old_path = old_path->next;
		}
	to_open = g_list_length(path) - 1;
	to_close = g_list_length(old_path) - 1;

	if (to_close > 0)
		{
		old_path = g_list_last(old_path);
		old_path = old_path->prev;
		}

	for (i =  0; i < to_close; i++)
		{
		auto name = static_cast<gchar *>(old_path->data);
		if (g_str_has_suffix(name, "Section"))
			{
			g_string_append(desc,	"      </placeholder>");
			}
		else if (g_str_has_suffix(name, "Menu"))
			{
			g_string_append(desc,	"    </menu>");
			}
		else
			{
			g_warning("invalid menu path item %s", name);
			}
		old_path = old_path->prev;
		}

	for (i =  0; i < to_open; i++)
		{
		auto name = static_cast<gchar *>(path->data);
		if (g_str_has_suffix(name, "Section"))
			{
			g_string_append_printf(desc,	"      <placeholder name='%s'>", name);
			}
		else if (g_str_has_suffix(name, "Menu"))
			{
			g_string_append_printf(desc,	"    <menu action='%s'>", name);
			}
		else
			{
			g_warning("invalid menu path item %s", name);
			}
		path = path->next;
		}

	if (path)
		g_string_append_printf(desc, "      <menuitem action='%s'/>", static_cast<gchar *>(path->data));
}

static void layout_actions_setup_editors(LayoutWindow *lw)
{
	GError *error;
	GList *editors_list;
	GList *work;
	GList *old_path;
	GString *desc;

	if (lw->ui_editors_id)
		{
		gq_gtk_ui_manager_remove_ui(lw->ui_manager, lw->ui_editors_id);
		}

	if (lw->action_group_editors)
		{
		gq_gtk_ui_manager_remove_action_group(lw->ui_manager, lw->action_group_editors);
		g_object_unref(lw->action_group_editors);
		}
	lw->action_group_editors = gq_gtk_action_group_new("MenuActionsExternal");
	gq_gtk_ui_manager_insert_action_group(lw->ui_manager, lw->action_group_editors, 1);

	/* lw->action_group_editors contains translated entries, no translate func is required */
	desc = g_string_new(
				"<ui>"
				"  <menubar name='MainMenu'>");

	if (options->hamburger_menu)
		{
		g_string_append(desc, "    <menu action='OpenMenu'>");
		}

	editors_list = editor_list_get();

	old_path = nullptr;
	work = editors_list;
	while (work)
		{
		GList *path;
		auto editor = static_cast<EditorDescription *>(work->data);
		GtkActionEntry entry = { editor->key,
		                         nullptr,
		                         editor->name,
		                         editor->hotkey,
		                         editor->comment ? editor->comment : editor->name,
		                         G_CALLBACK(layout_menu_edit_cb) };

		if (editor->icon)
			{
			entry.stock_id = editor->key;
			}
		gq_gtk_action_group_add_actions(lw->action_group_editors, &entry, 1, lw);

		GList *button_list;
		GList *work_button_list;

		button_list = gtk_container_get_children(GTK_CONTAINER(lw->toolbar[TOOLBAR_MAIN]));
		work_button_list = button_list;

		while (work_button_list)
			{
			if (g_strcmp0(gtk_widget_get_tooltip_text(GTK_WIDGET(work_button_list->data)), editor->key) == 0)
				{
				GtkWidget *image = nullptr;
				if (editor->icon)
					{
					image = gtk_image_new_from_stock(editor->key, GTK_ICON_SIZE_BUTTON);
					}
				else
					{
					image = gtk_image_new_from_icon_name(GQ_ICON_MISSING_IMAGE, GTK_ICON_SIZE_BUTTON);
					}
				gtk_button_set_image(GTK_BUTTON(work_button_list->data), GTK_WIDGET(image));
				gtk_widget_set_tooltip_text(GTK_WIDGET(work_button_list->data), editor->name);
				}
			work_button_list = work_button_list->next;
			}

		g_list_free(button_list);

		path = layout_actions_editor_menu_path(editor);
		layout_actions_editor_add(desc, path, old_path);

		g_list_free_full(old_path, g_free);
		old_path = path;
		work = work->next;
		}

	layout_actions_editor_add(desc, nullptr, old_path);
	g_list_free_full(old_path, g_free);

	if (options->hamburger_menu)
		{
		g_string_append(desc, "</menu>");
		}

	g_string_append(desc,"  </menubar>"
				"</ui>" );

	error = nullptr;

	lw->ui_editors_id = gq_gtk_ui_manager_add_ui_from_string(lw->ui_manager, desc->str, -1, &error);
	if (!lw->ui_editors_id)
		{
		g_message("building menus failed: %s", error->message);
		g_error_free(error);
		exit(EXIT_FAILURE);
		}
	g_string_free(desc, TRUE);
	g_list_free(editors_list);
}

void create_toolbars(LayoutWindow *lw)
{
	gint i;

	for (i = 0; i < TOOLBAR_COUNT; i++)
		{
		layout_actions_toolbar(lw, static_cast<ToolbarType>(i));
		layout_toolbar_clear(lw, static_cast<ToolbarType>(i));
		layout_toolbar_add_default(lw, static_cast<ToolbarType>(i));
		}
}

void layout_actions_setup(LayoutWindow *lw)
{
	GError *error;

	DEBUG_1("%s layout_actions_setup: start", get_exec_time());
	if (lw->ui_manager) return;

	lw->action_group = gq_gtk_action_group_new("MenuActions");
	gq_gtk_action_group_set_translate_func(lw->action_group, menu_translate, nullptr, nullptr);

	gq_gtk_action_group_add_actions(lw->action_group,
				     menu_entries, G_N_ELEMENTS(menu_entries), lw);
	gq_gtk_action_group_add_toggle_actions(lw->action_group,
					    menu_toggle_entries, G_N_ELEMENTS(menu_toggle_entries), lw);
	gq_gtk_action_group_add_radio_actions(lw->action_group,
					   menu_radio_entries, G_N_ELEMENTS(menu_radio_entries),
					   0, G_CALLBACK(layout_menu_list_cb), lw);
	gq_gtk_action_group_add_radio_actions(lw->action_group,
					   menu_split_radio_entries, G_N_ELEMENTS(menu_split_radio_entries),
					   0, G_CALLBACK(layout_menu_split_cb), lw);
	gq_gtk_action_group_add_toggle_actions(lw->action_group,
					   menu_view_dir_toggle_entries, G_N_ELEMENTS(menu_view_dir_toggle_entries),
					    lw);
	gq_gtk_action_group_add_radio_actions(lw->action_group,
					   menu_color_radio_entries, COLOR_PROFILE_FILE + COLOR_PROFILE_INPUTS,
					   0, G_CALLBACK(layout_color_menu_input_cb), lw);
	gq_gtk_action_group_add_radio_actions(lw->action_group,
					   menu_histogram_channel, G_N_ELEMENTS(menu_histogram_channel),
					   0, G_CALLBACK(layout_menu_histogram_channel_cb), lw);
	gq_gtk_action_group_add_radio_actions(lw->action_group,
					   menu_histogram_mode, G_N_ELEMENTS(menu_histogram_mode),
					   0, G_CALLBACK(layout_menu_histogram_mode_cb), lw);
	gq_gtk_action_group_add_radio_actions(lw->action_group,
					   menu_stereo_mode_entries, G_N_ELEMENTS(menu_stereo_mode_entries),
					   0, G_CALLBACK(layout_menu_stereo_mode_cb), lw);
	gq_gtk_action_group_add_radio_actions(lw->action_group,
					   menu_draw_rectangle_aspect_ratios, G_N_ELEMENTS(menu_draw_rectangle_aspect_ratios),
					   0, G_CALLBACK(layout_menu_draw_rectangle_aspect_ratio_cb), lw);


	lw->ui_manager = gq_gtk_ui_manager_new();
	gq_gtk_ui_manager_set_add_tearoffs(lw->ui_manager, TRUE);
	gq_gtk_ui_manager_insert_action_group(lw->ui_manager, lw->action_group, 0);

	DEBUG_1("%s layout_actions_setup: add menu", get_exec_time());
	error = nullptr;

	if (!gq_gtk_ui_manager_add_ui_from_resource(lw->ui_manager, options->hamburger_menu ? GQ_RESOURCE_PATH_UI "/menu-hamburger.ui" : GQ_RESOURCE_PATH_UI "/menu-classic.ui" , &error))
		{
		g_message("building menus failed: %s", error->message);
		g_error_free(error);
		exit(EXIT_FAILURE);
		}

	DEBUG_1("%s layout_actions_setup: marks", get_exec_time());
	layout_actions_setup_marks(lw);

	DEBUG_1("%s layout_actions_setup: editors", get_exec_time());
	layout_actions_setup_editors(lw);

	DEBUG_1("%s layout_actions_setup: status_update_write", get_exec_time());
	layout_util_status_update_write(lw);

	DEBUG_1("%s layout_actions_setup: actions_add_window", get_exec_time());
	layout_actions_add_window(lw, lw->window);
	DEBUG_1("%s layout_actions_setup: end", get_exec_time());
}

static gint layout_editors_reload_idle_id = -1;
static GList *layout_editors_desktop_files = nullptr;

static gboolean layout_editors_reload_idle_cb(gpointer)
{
	if (!layout_editors_desktop_files)
		{
		DEBUG_1("%s layout_editors_reload_idle_cb: get_desktop_files", get_exec_time());
		layout_editors_desktop_files = editor_get_desktop_files();
		return G_SOURCE_CONTINUE;
		}

	editor_read_desktop_file(static_cast<const gchar *>(layout_editors_desktop_files->data));
	g_free(layout_editors_desktop_files->data);
	layout_editors_desktop_files = g_list_delete_link(layout_editors_desktop_files, layout_editors_desktop_files);


	if (!layout_editors_desktop_files)
		{
		GList *work;
		DEBUG_1("%s layout_editors_reload_idle_cb: setup_editors", get_exec_time());
		editor_table_finish();

		work = layout_window_list;
		while (work)
			{
			auto lw = static_cast<LayoutWindow *>(work->data);
			work = work->next;
			layout_actions_setup_editors(lw);
			if (lw->bar_sort_enabled)
				{
				layout_bar_sort_toggle(lw);
				}
			}

		DEBUG_1("%s layout_editors_reload_idle_cb: setup_editors done", get_exec_time());

		/* The toolbars need to be regenerated in case they contain a plugin */
		LayoutWindow *lw = nullptr;
		layout_valid(&lw);

		toolbar_select_new(lw, TOOLBAR_MAIN);
		toolbar_apply(TOOLBAR_MAIN);

		toolbar_select_new(lw, TOOLBAR_STATUS);
		toolbar_apply(TOOLBAR_STATUS);

		layout_editors_reload_idle_id = -1;
		return G_SOURCE_REMOVE;
		}
	return G_SOURCE_CONTINUE;
}

void layout_editors_reload_start()
{
	DEBUG_1("%s layout_editors_reload_start", get_exec_time());

	if (layout_editors_reload_idle_id != -1)
		{
		g_source_remove(layout_editors_reload_idle_id);
		g_list_free_full(layout_editors_desktop_files, g_free);
		}

	editor_table_clear();
	layout_editors_reload_idle_id = g_idle_add(layout_editors_reload_idle_cb, nullptr);
}

void layout_editors_reload_finish()
{
	if (layout_editors_reload_idle_id != -1)
		{
		DEBUG_1("%s layout_editors_reload_finish", get_exec_time());
		g_source_remove(layout_editors_reload_idle_id);
		while (layout_editors_reload_idle_id != -1)
			{
			layout_editors_reload_idle_cb(nullptr);
			}
		}
}

void layout_actions_add_window(LayoutWindow *lw, GtkWidget *window)
{
	GtkAccelGroup *group;

	if (!lw->ui_manager) return;

	group = gq_gtk_ui_manager_get_accel_group(lw->ui_manager);
	gtk_window_add_accel_group(GTK_WINDOW(window), group);
}

GtkWidget *layout_actions_menu_bar(LayoutWindow *lw)
{
	if (lw->menu_bar) return lw->menu_bar;
	lw->menu_bar = gq_gtk_ui_manager_get_widget(lw->ui_manager, "/MainMenu");
	g_object_ref(lw->menu_bar);
	return lw->menu_bar;
}

GtkWidget *layout_actions_toolbar(LayoutWindow *lw, ToolbarType type)
{
	if (lw->toolbar[type]) return lw->toolbar[type];

	lw->toolbar[type] = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	gtk_widget_show(lw->toolbar[type]);
	g_object_ref(lw->toolbar[type]);
	return lw->toolbar[type];
}

GtkWidget *layout_actions_menu_tool_bar(LayoutWindow *lw)
{
	GtkWidget *menu_bar;
	GtkWidget *toolbar;

	if (lw->menu_tool_bar) return lw->menu_tool_bar;

	toolbar = layout_actions_toolbar(lw, TOOLBAR_MAIN);
	DEBUG_NAME(toolbar);
	lw->menu_tool_bar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	if (!options->hamburger_menu)
		{
		menu_bar = layout_actions_menu_bar(lw);
		DEBUG_NAME(menu_bar);
		gq_gtk_box_pack_start(GTK_BOX(lw->menu_tool_bar), menu_bar, FALSE, FALSE, 0);
		}

	gq_gtk_box_pack_start(GTK_BOX(lw->menu_tool_bar), toolbar, FALSE, FALSE, 0);

	g_object_ref(lw->menu_tool_bar);
	return lw->menu_tool_bar;
}

static void toolbar_clear_cb(GtkWidget *widget, gpointer)
{
	GtkAction *action;

	if (GTK_IS_BUTTON(widget))
		{
		action = static_cast<GtkAction *>(g_object_get_data(G_OBJECT(widget), "action"));
		if (g_object_get_data(G_OBJECT(widget), "id") )
			{
			g_signal_handler_disconnect(action, GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "id")));
			}
		}
	gq_gtk_widget_destroy(widget);
}

void layout_toolbar_clear(LayoutWindow *lw, ToolbarType type)
{
	if (lw->toolbar_merge_id[type])
		{
		gq_gtk_ui_manager_remove_ui(lw->ui_manager, lw->toolbar_merge_id[type]);
		gq_gtk_ui_manager_ensure_update(lw->ui_manager);
		}
	g_list_free_full(lw->toolbar_actions[type], g_free);
	lw->toolbar_actions[type] = nullptr;

	lw->toolbar_merge_id[type] = gq_gtk_ui_manager_new_merge_id(lw->ui_manager);

	if (lw->toolbar[type])
		{
		gtk_container_foreach(GTK_CONTAINER(lw->toolbar[type]), (GtkCallback)G_CALLBACK(toolbar_clear_cb), nullptr);
		}
}

static void action_radio_changed_cb(GtkAction *action, GtkAction *current, gpointer data)
{
	auto button = static_cast<GtkToggleButton *>(data);

	if (action == current )
		{
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
		}
	else
		{
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), FALSE);
		}
}

static void action_toggle_activate_cb(GtkAction* self, gpointer data)
{
	auto button = static_cast<GtkToggleButton *>(data);

	if (gq_gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(self)) != gtk_toggle_button_get_active(button))
		{
		gtk_toggle_button_set_active(button, gq_gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(self)));
		}
}

static gboolean toolbar_button_press_event_cb(GtkWidget *, GdkEvent *, gpointer data)
{
	gq_gtk_action_activate(GTK_ACTION(data));

	return TRUE;
}

void layout_toolbar_add(LayoutWindow *lw, ToolbarType type, const gchar *action_name)
{
	const gchar *path = nullptr;
	const gchar *tooltip_text = nullptr;
	GtkAction *action;
	GtkWidget *action_icon = nullptr;
	GtkWidget *button;
	gulong id;

	if (!action_name || !lw->ui_manager) return;

	if (!lw->toolbar[type])
		{
		return;
		}

	switch (type)
		{
		case TOOLBAR_MAIN:
			path = "/ToolBar";
			break;
		case TOOLBAR_STATUS:
			path = "/StatusBar";
			break;
		default:
			break;
		}

	if (g_str_has_suffix(action_name, ".desktop"))
		{
		/* this may be called before the external editors are read
		   create a dummy action for now */
		if (!lw->action_group_editors)
			{
			lw->action_group_editors = gq_gtk_action_group_new("MenuActionsExternal");
			gq_gtk_ui_manager_insert_action_group(lw->ui_manager, lw->action_group_editors, 1);
			}
		if (!gq_gtk_action_group_get_action(lw->action_group_editors, action_name))
			{
			GtkActionEntry entry = { action_name,
			                         GQ_ICON_MISSING_IMAGE,
			                         action_name,
			                         nullptr,
			                         nullptr,
			                         nullptr
			                       };
			DEBUG_1("Creating temporary action %s", action_name);
			gq_gtk_action_group_add_actions(lw->action_group_editors, &entry, 1, lw);
			}
		}

	if (g_strcmp0(action_name, "Separator") == 0)
		{
		button = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
		}
	else
		{
		if (g_str_has_suffix(action_name, ".desktop"))
			{
			action = gq_gtk_action_group_get_action(lw->action_group_editors, action_name);

			/** @FIXME Using tootip as a flag to layout_actions_setup_editors()
			 * is not a good way.
			 */
			tooltip_text = gtk_action_get_label(action);
			}
		else
			{
			action = gq_gtk_action_group_get_action(lw->action_group, action_name);

			tooltip_text = gq_gtk_action_get_tooltip(action);
			}

		action_icon = gq_gtk_action_create_icon(action, GTK_ICON_SIZE_SMALL_TOOLBAR);

		gq_gtk_ui_manager_add_ui(lw->ui_manager, lw->toolbar_merge_id[type], path, action_name, action_name, GTK_UI_MANAGER_TOOLITEM, FALSE);

		if (GQ_GTK_IS_RADIO_ACTION(action) || GQ_GTK_IS_TOGGLE_ACTION(action))
			{
			button = gtk_toggle_button_new();
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), gq_gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)));
			}
		else
			{
			button = gtk_button_new();
			}

		if (action_icon)
			{
			gtk_button_set_image(GTK_BUTTON(button), action_icon);
			}
		else
			{
			gtk_button_set_label(GTK_BUTTON(button), action_name);
			}

		gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
		gtk_widget_set_tooltip_text(button, tooltip_text);

		if (GQ_GTK_IS_RADIO_ACTION(action))
			{
			id = g_signal_connect(G_OBJECT(action), "changed", G_CALLBACK(action_radio_changed_cb), button);
			g_object_set_data(G_OBJECT(button), "id", GUINT_TO_POINTER(id));
			}
		else if (GQ_GTK_IS_TOGGLE_ACTION(action))
			{
			id = g_signal_connect(G_OBJECT(action), "activate", G_CALLBACK(action_toggle_activate_cb), button);
			g_object_set_data(G_OBJECT(button), "id", GUINT_TO_POINTER(id));
			}

		g_signal_connect(G_OBJECT(button), "button_press_event", G_CALLBACK(toolbar_button_press_event_cb), action);
		g_object_set_data(G_OBJECT(button), "action", action);
		}

	gq_gtk_container_add(GTK_WIDGET(lw->toolbar[type]), GTK_WIDGET(button));
	gtk_widget_show(GTK_WIDGET(button));

	lw->toolbar_actions[type] = g_list_append(lw->toolbar_actions[type], g_strdup(action_name));
}

void layout_toolbar_add_default(LayoutWindow *lw, ToolbarType type)
{
	LayoutWindow *lw_first;
	GList *work_action;

	switch (type)
		{
		case TOOLBAR_MAIN:
			if (layout_window_list)
				{
				lw_first = static_cast<LayoutWindow *>(layout_window_list->data);
				if (lw_first->toolbar_actions[TOOLBAR_MAIN])
					{
					work_action = lw_first->toolbar_actions[type];
					while (work_action)
						{
						auto action = static_cast<gchar *>(work_action->data);
						work_action = work_action->next;
						layout_toolbar_add(lw, type, action);
						}
					}
				else
					{
					layout_toolbar_add(lw, type, "Thumbnails");
					layout_toolbar_add(lw, type, "Back");
					layout_toolbar_add(lw, type, "Forward");
					layout_toolbar_add(lw, type, "Up");
					layout_toolbar_add(lw, type, "Home");
					layout_toolbar_add(lw, type, "Refresh");
					layout_toolbar_add(lw, type, "ZoomIn");
					layout_toolbar_add(lw, type, "ZoomOut");
					layout_toolbar_add(lw, type, "ZoomFit");
					layout_toolbar_add(lw, type, "Zoom100");
					layout_toolbar_add(lw, type, "Preferences");
					layout_toolbar_add(lw, type, "FloatTools");
					}
				}
			else
				{
				layout_toolbar_add(lw, type, "Thumbnails");
				layout_toolbar_add(lw, type, "Back");
				layout_toolbar_add(lw, type, "Forward");
				layout_toolbar_add(lw, type, "Up");
				layout_toolbar_add(lw, type, "Home");
				layout_toolbar_add(lw, type, "Refresh");
				layout_toolbar_add(lw, type, "ZoomIn");
				layout_toolbar_add(lw, type, "ZoomOut");
				layout_toolbar_add(lw, type, "ZoomFit");
				layout_toolbar_add(lw, type, "Zoom100");
				layout_toolbar_add(lw, type, "Preferences");
				layout_toolbar_add(lw, type, "FloatTools");
				}
			break;
		case TOOLBAR_STATUS:
			if (layout_window_list)
				{
				lw_first = static_cast<LayoutWindow *>(layout_window_list->data);
				if (lw_first->toolbar_actions[TOOLBAR_STATUS])
					{
					work_action = lw_first->toolbar_actions[type];
					while (work_action)
						{
						auto action = static_cast<gchar *>(work_action->data);
						work_action = work_action->next;
						layout_toolbar_add(lw, type, action);
						}
					}
				else
					{
					layout_toolbar_add(lw, type, "ExifRotate");
					layout_toolbar_add(lw, type, "ShowInfoPixel");
					layout_toolbar_add(lw, type, "UseColorProfiles");
					layout_toolbar_add(lw, type, "SaveMetadata");
					}
				}
			else
				{
				layout_toolbar_add(lw, type, "ExifRotate");
				layout_toolbar_add(lw, type, "ShowInfoPixel");
				layout_toolbar_add(lw, type, "UseColorProfiles");
				layout_toolbar_add(lw, type, "SaveMetadata");
				}
			break;
		default:
			break;
		}
}



void layout_toolbar_write_config(LayoutWindow *lw, ToolbarType type, GString *outstr, gint indent)
{
	const gchar *name = nullptr;
	GList *work = lw->toolbar_actions[type];

	switch (type)
		{
		case TOOLBAR_MAIN:
			name = "toolbar";
			break;
		case TOOLBAR_STATUS:
			name = "statusbar";
			break;
		default:
			break;
		}

	WRITE_NL(); WRITE_STRING("<%s>", name);
	indent++;
	WRITE_NL(); WRITE_STRING("<clear/>");
	while (work)
		{
		auto action = static_cast<gchar *>(work->data);
		work = work->next;
		WRITE_NL(); WRITE_STRING("<toolitem ");
		write_char_option(outstr, indent + 1, "action", action);
		WRITE_STRING("/>");
		}
	indent--;
	WRITE_NL(); WRITE_STRING("</%s>", name);
}

void layout_toolbar_add_from_config(LayoutWindow *lw, ToolbarType type, const char **attribute_names, const gchar **attribute_values)
{
	gchar *action = nullptr;

	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_CHAR_FULL("action", action)) continue;

		log_printf("unknown attribute %s = %s\n", option, value);
		}

	layout_toolbar_add(lw, type, action);
	g_free(action);
}

/*
 *-----------------------------------------------------------------------------
 * misc
 *-----------------------------------------------------------------------------
 */

void layout_util_status_update_write(LayoutWindow *lw)
{
	GtkAction *action;
	gint n = metadata_queue_length();
	action = gq_gtk_action_group_get_action(lw->action_group, "SaveMetadata");
	gq_gtk_action_set_sensitive(action, n > 0);
	if (n > 0)
		{
		gchar *buf = g_strdup_printf(_("Number of files with unsaved metadata: %d"), n);
		g_object_set(G_OBJECT(action), "tooltip", buf, NULL);
		g_free(buf);
		}
	else
		{
		g_object_set(G_OBJECT(action), "tooltip", _("No unsaved metadata"), NULL);
		}
}

void layout_util_status_update_write_all()
{
	GList *work;

	work = layout_window_list;
	while (work)
		{
		auto lw = static_cast<LayoutWindow *>(work->data);
		work = work->next;

		layout_util_status_update_write(lw);
		}
}

static gchar *layout_color_name_parse(const gchar *name)
{
	if (!name || !*name) return g_strdup(_("Empty"));
	return g_strdelimit(g_strdup(name), "_", '-');
}

void layout_util_sync_color(LayoutWindow *lw)
{
	GtkAction *action;
	gint input = 0;
	gboolean use_color;
	gboolean use_image = FALSE;
	gint i;
	gchar action_name[15];
#if HAVE_LCMS
	gchar *image_profile;
	gchar *screen_profile;
#endif

	if (!lw->action_group) return;
	if (!layout_image_color_profile_get(lw, input, use_image)) return;

	use_color = layout_image_color_profile_get_use(lw);

	action = gq_gtk_action_group_get_action(lw->action_group, "UseColorProfiles");
#if HAVE_LCMS
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), use_color);
	if (layout_image_color_profile_get_status(lw, &image_profile, &screen_profile))
		{
		gchar *buf;
		buf = g_strdup_printf(_("Image profile: %s\nScreen profile: %s"), image_profile, screen_profile);
		g_object_set(G_OBJECT(action), "tooltip", buf, NULL);
		g_free(image_profile);
		g_free(screen_profile);
		g_free(buf);
		}
	else
		{
		g_object_set(G_OBJECT(action), "tooltip", _("Click to enable color management"), NULL);
		}
#else
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), FALSE);
	gq_gtk_action_set_sensitive(action, FALSE);
	g_object_set(G_OBJECT(action), "tooltip", _("Color profiles not supported"), NULL);
#endif

	action = gq_gtk_action_group_get_action(lw->action_group, "UseImageProfile");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), use_image);
	gq_gtk_action_set_sensitive(action, use_color);

	for (i = 0; i < COLOR_PROFILE_FILE + COLOR_PROFILE_INPUTS; i++)
		{
		sprintf(action_name, "ColorProfile%d", i);
		action = gq_gtk_action_group_get_action(lw->action_group, action_name);

		if (i >= COLOR_PROFILE_FILE)
			{
			const gchar *name = options->color_profile.input_name[i - COLOR_PROFILE_FILE];
			const gchar *file = options->color_profile.input_file[i - COLOR_PROFILE_FILE];
			gchar *end;
			gchar *buf;

			if (!name || !name[0]) name = filename_from_path(file);

			end = layout_color_name_parse(name);
			buf = g_strdup_printf(_("Input _%d: %s"), i, end);
			g_free(end);

			g_object_set(G_OBJECT(action), "label", buf, NULL);
			g_free(buf);

			gq_gtk_action_set_visible(action, file && file[0]);
			}

		gq_gtk_action_set_sensitive(action, !use_image);
		gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), (i == input));
		}

	action = gq_gtk_action_group_get_action(lw->action_group, "Grayscale");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), layout_image_get_desaturate(lw));
}

void layout_util_sync_file_filter(LayoutWindow *lw)
{
	GtkAction *action;

	if (!lw->action_group) return;

	action = gq_gtk_action_group_get_action(lw->action_group, "ShowFileFilter");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.show_file_filter);
}

void layout_util_sync_marks(LayoutWindow *lw)
{
	GtkAction *action;

	if (!lw->action_group) return;

	action = gq_gtk_action_group_get_action(lw->action_group, "ShowMarks");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.show_marks);
}

static void layout_util_sync_views(LayoutWindow *lw)
{
	GtkAction *action;
	OsdShowFlags osd_flags = image_osd_get(lw->image);

	if (!lw->action_group) return;

	action = gq_gtk_action_group_get_action(lw->action_group, "FolderTree");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.dir_view_type);

	action = gq_gtk_action_group_get_action(lw->action_group, "SplitSingle");
	gq_gtk_radio_action_set_current_value(GTK_RADIO_ACTION(action), lw->split_mode);

	action = gq_gtk_action_group_get_action(lw->action_group, "SplitNextPane");
	gq_gtk_action_set_sensitive(action, !(lw->split_mode == SPLIT_NONE));
	action = gq_gtk_action_group_get_action(lw->action_group, "SplitPreviousPane");
	gq_gtk_action_set_sensitive(action, !(lw->split_mode == SPLIT_NONE));
	action = gq_gtk_action_group_get_action(lw->action_group, "SplitUpPane");
	gq_gtk_action_set_sensitive(action, !(lw->split_mode == SPLIT_NONE));
	action = gq_gtk_action_group_get_action(lw->action_group, "SplitDownPane");
	gq_gtk_action_set_sensitive(action, !(lw->split_mode == SPLIT_NONE));

	action = gq_gtk_action_group_get_action(lw->action_group, "SplitPaneSync");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.split_pane_sync);

	action = gq_gtk_action_group_get_action(lw->action_group, "ViewIcons");
	gq_gtk_radio_action_set_current_value(GTK_RADIO_ACTION(action), lw->options.file_view_type);

	action = gq_gtk_action_group_get_action(lw->action_group, "CropNone");
	gq_gtk_radio_action_set_current_value(GTK_RADIO_ACTION(action), options->rectangle_draw_aspect_ratio);

	action = gq_gtk_action_group_get_action(lw->action_group, "FloatTools");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.tools_float);

	action = gq_gtk_action_group_get_action(lw->action_group, "SBar");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), layout_bar_enabled(lw));

	action = gq_gtk_action_group_get_action(lw->action_group, "SBarSort");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), layout_bar_sort_enabled(lw));

	action = gq_gtk_action_group_get_action(lw->action_group, "HideSelectableToolbars");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.selectable_toolbars_hidden);

	action = gq_gtk_action_group_get_action(lw->action_group, "ShowInfoPixel");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.show_info_pixel);

	action = gq_gtk_action_group_get_action(lw->action_group, "SlideShow");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), layout_image_slideshow_active(lw));

	action = gq_gtk_action_group_get_action(lw->action_group, "IgnoreAlpha");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.ignore_alpha);

	action = gq_gtk_action_group_get_action(lw->action_group, "Animate");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.animate);

	action = gq_gtk_action_group_get_action(lw->action_group, "ImageOverlay");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), osd_flags != OSD_SHOW_NOTHING);

	action = gq_gtk_action_group_get_action(lw->action_group, "ImageHistogram");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), osd_flags & OSD_SHOW_HISTOGRAM);

	action = gq_gtk_action_group_get_action(lw->action_group, "ExifRotate");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), options->image.exif_rotate_enable);

	action = gq_gtk_action_group_get_action(lw->action_group, "OverUnderExposed");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), options->overunderexposed);

	action = gq_gtk_action_group_get_action(lw->action_group, "DrawRectangle");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), options->draw_rectangle);

	action = gq_gtk_action_group_get_action(lw->action_group, "RectangularSelection");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), options->collections.rectangular_selection);

	action = gq_gtk_action_group_get_action(lw->action_group, "ShowFileFilter");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.show_file_filter);

	action = gq_gtk_action_group_get_action(lw->action_group, "HideBars");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), (lw->options.bars_state.hidden));

	if (osd_flags & OSD_SHOW_HISTOGRAM)
		{
		action = gq_gtk_action_group_get_action(lw->action_group, "HistogramChanR");
		gq_gtk_radio_action_set_current_value(GTK_RADIO_ACTION(action), image_osd_histogram_get_channel(lw->image));

		action = gq_gtk_action_group_get_action(lw->action_group, "HistogramModeLin");
		gq_gtk_radio_action_set_current_value(GTK_RADIO_ACTION(action), image_osd_histogram_get_mode(lw->image));
		}

	action = gq_gtk_action_group_get_action(lw->action_group, "ConnectZoomMenu");
	gq_gtk_action_set_sensitive(action, lw->split_mode != SPLIT_NONE);

	// @todo `which` is deprecated, use command -v
	gboolean is_write_rotation = !runcmd("which exiftran >/dev/null 2>&1")
	                          && !runcmd("which mogrify >/dev/null 2>&1")
	                          && !options->metadata.write_orientation;
	action = gq_gtk_action_group_get_action(lw->action_group, "WriteRotation");
	gq_gtk_action_set_sensitive(action, is_write_rotation);
	action = gq_gtk_action_group_get_action(lw->action_group, "WriteRotationKeepDate");
	gq_gtk_action_set_sensitive(action, is_write_rotation);

	action = gq_gtk_action_group_get_action(lw->action_group, "StereoAuto");
	gq_gtk_radio_action_set_current_value(GTK_RADIO_ACTION(action), layout_image_stereo_pixbuf_get(lw));

	layout_util_sync_marks(lw);
	layout_util_sync_color(lw);
	layout_image_set_ignore_alpha(lw, lw->options.ignore_alpha);
}

void layout_util_sync_thumb(LayoutWindow *lw)
{
	GtkAction *action;

	if (!lw->action_group) return;

	action = gq_gtk_action_group_get_action(lw->action_group, "Thumbnails");
	gq_gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), lw->options.show_thumbnails);
	g_object_set(action, "sensitive", (lw->options.file_view_type == FILEVIEW_LIST), NULL);
}

void layout_util_sync(LayoutWindow *lw)
{
	layout_util_sync_views(lw);
	layout_util_sync_thumb(lw);
	layout_menu_collection_recent_update(lw);
	layout_menu_collection_open_update(lw);
}

/**
 * @brief Checks if event key is mapped to Help
 * @param event
 * @returns
 *
 * Used to check if the user has re-mapped the Help key
 * in Preferences/Keyboard
 *
 * Note: help_key.accel_mods and event->state
 * differ in the higher bits
 */
gboolean is_help_key(GdkEventKey *event)
{
	GtkAccelKey help_key;
	gboolean ret = FALSE;
	guint mask = GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK;

	if (gtk_accel_map_lookup_entry("<Actions>/MenuActions/HelpContents", &help_key))
		{
		if (help_key.accel_key == event->keyval &&
					(help_key.accel_mods & mask) == (event->state & mask))
			{
			ret = TRUE;
			}
		}

	return ret;
}

/*
 *-----------------------------------------------------------------------------
 * sidebars
 *-----------------------------------------------------------------------------
 */

static gboolean layout_bar_enabled(LayoutWindow *lw)
{
	return lw->bar && gtk_widget_get_visible(lw->bar);
}

static void layout_bar_destroyed(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	lw->bar = nullptr;
/*
    do not call layout_util_sync_views(lw) here
    this is called either when whole layout is destroyed - no need for update
    or when the bar is replaced - sync is called by upper function at the end of whole operation

*/
}

static void layout_bar_set_default(LayoutWindow *lw)
{
	GtkWidget *bar;

	if (!lw->utility_box) return;

	bar = bar_new(lw);
	DEBUG_NAME(bar);

	layout_bar_set(lw, bar);

	bar_populate_default(bar);
}

static void layout_bar_close(LayoutWindow *lw)
{
	if (lw->bar)
		{
		bar_close(lw->bar);
		lw->bar = nullptr;
		}
}


void layout_bar_set(LayoutWindow *lw, GtkWidget *bar)
{
	if (!lw->utility_box) return;

	layout_bar_close(lw); /* if any */

	if (!bar) return;
	lw->bar = bar;

	g_signal_connect(G_OBJECT(lw->bar), "destroy",
			 G_CALLBACK(layout_bar_destroyed), lw);

	gtk_paned_pack2(GTK_PANED(lw->utility_paned), lw->bar, FALSE, TRUE);

	bar_set_fd(lw->bar, layout_image_get_fd(lw));
}


void layout_bar_toggle(LayoutWindow *lw)
{
	if (layout_bar_enabled(lw))
		{
		gtk_widget_hide(lw->bar);
		}
	else
		{
		if (!lw->bar)
			{
			layout_bar_set_default(lw);
			}
		gtk_widget_show(lw->bar);
		bar_set_fd(lw->bar, layout_image_get_fd(lw));
		}
	layout_util_sync_views(lw);
}

static void layout_bar_new_image(LayoutWindow *lw)
{
	if (!layout_bar_enabled(lw)) return;

	bar_set_fd(lw->bar, layout_image_get_fd(lw));
}

static void layout_bar_new_selection(LayoutWindow *lw, gint count)
{
	if (!layout_bar_enabled(lw)) return;

	bar_notify_selection(lw->bar, count);
}

static gboolean layout_bar_sort_enabled(LayoutWindow *lw)
{
	return lw->bar_sort && gtk_widget_get_visible(lw->bar_sort);
}


static void layout_bar_sort_destroyed(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	lw->bar_sort = nullptr;

/*
    do not call layout_util_sync_views(lw) here
    this is called either when whole layout is destroyed - no need for update
    or when the bar is replaced - sync is called by upper function at the end of whole operation

*/
}

static void layout_bar_sort_set_default(LayoutWindow *lw)
{
	GtkWidget *bar;

	if (!lw->utility_box) return;

	bar = bar_sort_new_default(lw);

	layout_bar_sort_set(lw, bar);
}

static void layout_bar_sort_close(LayoutWindow *lw)
{
	if (lw->bar_sort)
		{
		bar_sort_close(lw->bar_sort);
		lw->bar_sort = nullptr;
		}
}

void layout_bar_sort_set(LayoutWindow *lw, GtkWidget *bar)
{
	if (!lw->utility_box) return;

	layout_bar_sort_close(lw); /* if any */

	if (!bar) return;
	lw->bar_sort = bar;

	g_signal_connect(G_OBJECT(lw->bar_sort), "destroy",
			 G_CALLBACK(layout_bar_sort_destroyed), lw);

	gq_gtk_box_pack_end(GTK_BOX(lw->utility_box), lw->bar_sort, FALSE, FALSE, 0);
}

void layout_bar_sort_toggle(LayoutWindow *lw)
{
	if (layout_bar_sort_enabled(lw))
		{
		gtk_widget_hide(lw->bar_sort);
		}
	else
		{
		if (!lw->bar_sort)
			{
			layout_bar_sort_set_default(lw);
			}
		gtk_widget_show(lw->bar_sort);
		}
	layout_util_sync_views(lw);
}

static void layout_bars_hide_toggle(LayoutWindow *lw)
{
	if (lw->options.bars_state.hidden)
		{
		lw->options.bars_state.hidden = FALSE;
		if (lw->options.bars_state.sort)
			{
			if (lw->bar_sort)
				{
				gtk_widget_show(lw->bar_sort);
				}
			else
				{
				layout_bar_sort_set_default(lw);
				}
			}
		if (lw->options.bars_state.info)
			{
			gtk_widget_show(lw->bar);
			}
		layout_tools_float_set(lw, lw->options.tools_float,
									lw->options.bars_state.tools_hidden);
		}
	else
		{
		lw->options.bars_state.hidden = TRUE;
		lw->options.bars_state.sort = layout_bar_sort_enabled(lw);
		lw->options.bars_state.info = layout_bar_enabled(lw);
		lw->options.bars_state.tools_float = lw->options.tools_float;
		lw->options.bars_state.tools_hidden = lw->options.tools_hidden;

		if (lw->bar)
			{
			gtk_widget_hide(lw->bar);
			}

		if (lw->bar_sort)
			gtk_widget_hide(lw->bar_sort);
		layout_tools_float_set(lw, lw->options.tools_float, TRUE);
		}

	layout_util_sync_views(lw);
}

void layout_bars_new_image(LayoutWindow *lw)
{
	layout_bar_new_image(lw);

	if (lw->exif_window) advanced_exif_set_fd(lw->exif_window, layout_image_get_fd(lw));

	/* this should be called here to handle the metadata edited in bars */
	if (options->metadata.confirm_on_image_change)
		metadata_write_queue_confirm(FALSE, nullptr, nullptr);
}

void layout_bars_new_selection(LayoutWindow *lw, gint count)
{
	layout_bar_new_selection(lw, count);
}

GtkWidget *layout_bars_prepare(LayoutWindow *lw, GtkWidget *image)
{
	if (lw->utility_box) return lw->utility_box;
	lw->utility_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_GAP);
	lw->utility_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	DEBUG_NAME(lw->utility_paned);
	gq_gtk_box_pack_start(GTK_BOX(lw->utility_box), lw->utility_paned, TRUE, TRUE, 0);

	gtk_paned_pack1(GTK_PANED(lw->utility_paned), image, TRUE, FALSE);
	gtk_widget_show(lw->utility_paned);

	gtk_widget_show(image);

	g_object_ref(lw->utility_box);
	return lw->utility_box;
}

void layout_bars_close(LayoutWindow *lw)
{
	layout_bar_sort_close(lw);
	layout_bar_close(lw);
}

static gboolean layout_exif_window_destroy(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	lw->exif_window = nullptr;

	return TRUE;
}

void layout_exif_window_new(LayoutWindow *lw)
{
	if (lw->exif_window) return;

	lw->exif_window = advanced_exif_new(lw);
	if (!lw->exif_window) return;
	g_signal_connect(G_OBJECT(lw->exif_window), "destroy",
			 G_CALLBACK(layout_exif_window_destroy), lw);
	advanced_exif_set_fd(lw->exif_window, layout_image_get_fd(lw));
}

static void layout_search_and_run_window_new(LayoutWindow *lw)
{
	if (lw->sar_window)
		{
		gtk_window_present(GTK_WINDOW(lw->sar_window));
		return;
		}

	lw->sar_window = search_and_run_new(lw);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
