/*
 * Copyright (C) 2018 The Geeqie Team
 *
 * Author: Colin Clark
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

#include "print.h"

#include <cstddef>
#include <cstring>

#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <glib-object.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>

#include "compat.h"
#include "exif.h"
#include "filedata.h"
#include "image-load.h"
#include "intl.h"
#include "main-defines.h"
#include "options.h"
#include "osd.h"
#include "pixbuf-util.h"
#include "ui-fileops.h"
#include "ui-misc.h"

#define PRINT_SETTINGS "print_settings" // filename save printer settings
#define PAGE_SETUP "page_setup" // filename save page setup

/* padding between objects */
#define PRINT_TEXT_PADDING 3.0

/* method to use when scaling down image data */
#define PRINT_MAX_INTERP GDK_INTERP_BILINEAR

namespace
{

struct PrintWindow
{
	GtkWidget *vbox;
	GList *source_selection;

	gint job_page;
	GtkTextBuffer *page_text;
	gchar *template_string;
	GtkWidget *parent;
	ImageLoader	*job_loader;

	GList *print_pixbuf_queue;
	gboolean job_render_finished;
	GSList *image_group;
	GSList *page_group;
};

constexpr gint PRE_FORMATTED_COLUMNS = 4;

gint print_layout_page_count(PrintWindow *pw)
{
	gint images;

	images = g_list_length(pw->source_selection);

	if (images < 1 ) return 0;

	return images;
}

gboolean print_job_render_image(PrintWindow *pw);

void print_job_render_image_loader_done(ImageLoader *il, gpointer data)
{
	auto pw = static_cast<PrintWindow *>(data);
	GdkPixbuf *pixbuf;

	pixbuf = image_loader_get_pixbuf(il);

	g_object_ref(pixbuf);
	pw->print_pixbuf_queue = g_list_append(pw->print_pixbuf_queue, pixbuf);

	image_loader_free(pw->job_loader);
	pw->job_loader = nullptr;

	pw->job_page++;

	if (!print_job_render_image(pw))
		{
		pw->job_render_finished = TRUE;
		}
}

gboolean print_job_render_image(PrintWindow *pw)
{
	FileData *fd = nullptr;

	fd = static_cast<FileData *>(g_list_nth_data(pw->source_selection, pw->job_page));
	if (!fd) return FALSE;

	image_loader_free(pw->job_loader);
	pw->job_loader = nullptr;

	pw->job_loader = image_loader_new(fd);
	g_signal_connect(G_OBJECT(pw->job_loader), "done",
						(GCallback)print_job_render_image_loader_done, pw);

	if (!image_loader_start(pw->job_loader))
		{
		image_loader_free(pw->job_loader);
		pw->job_loader= nullptr;
		}

	return TRUE;
}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-parameter"
void font_activated_cb(GtkFontChooser *widget, gchar *fontname, gpointer option)
{
	option = fontname;

	gq_gtk_widget_destroy(GTK_WIDGET(widget));
}
#pragma GCC diagnostic pop

void font_response_cb(GtkDialog *dialog, int response_id, gpointer option)
{
	if (response_id == GTK_RESPONSE_OK)
		{
		g_free(option);
		option = gtk_font_chooser_get_font(GTK_FONT_CHOOSER(dialog));
		}

	gq_gtk_widget_destroy(GTK_WIDGET(dialog));
}

void print_set_font_cb(GtkWidget *widget, gpointer data)
{
	gpointer option;
	GtkWidget *dialog;

	if (g_strcmp0(static_cast<const gchar *>(data), "Image text font") == 0)
		{
		option = options->printer.image_font;
		}
	else
		{
		option = options->printer.page_font;
		}

	dialog = gtk_font_chooser_dialog_new(static_cast<const gchar *>(data), GTK_WINDOW(gtk_widget_get_toplevel(widget)));
	gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
	gtk_font_chooser_set_font(GTK_FONT_CHOOSER(dialog), static_cast<const gchar *>(option));

	g_signal_connect(dialog, "font-activated", G_CALLBACK(font_activated_cb), option);
	g_signal_connect(dialog, "response", G_CALLBACK(font_response_cb), option);

	gtk_widget_show(dialog);
}

gint set_toggle(GSList *list, TextPosition pos)
{
	GtkToggleButton *current_sel;
	GtkToggleButton *new_sel;
	gint new_pos = - 1;

	current_sel = static_cast<GtkToggleButton *>(g_slist_nth(list, pos)->data);
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(current_sel)))
		{
		new_pos = (pos - 1);
		if (new_pos < 0)
			{
			new_pos = HEADER_1;
			}
		new_sel = static_cast<GtkToggleButton *>(g_slist_nth(list, new_pos)->data);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(new_sel), TRUE);
		}
	return new_pos;
}

void image_text_position_cb(GtkWidget *widget, gpointer data, TextPosition pos)
{
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) return;

	auto pw = static_cast<PrintWindow *>(data);
	gint new_set = set_toggle(pw->page_group, pos);
	if (new_set >= 0)
		{
		options->printer.page_text_position = static_cast<TextPosition>(new_set);
		}

	options->printer.image_text_position = pos;
}

void image_text_position_h1_cb(GtkWidget *widget, gpointer data)
{
	image_text_position_cb(widget, data, HEADER_1);
}

void image_text_position_h2_cb(GtkWidget *widget, gpointer data)
{
	image_text_position_cb(widget, data, HEADER_2);
}

void image_text_position_f1_cb(GtkWidget *widget, gpointer data)
{
	image_text_position_cb(widget, data, FOOTER_1);
}

void image_text_position_f2_cb(GtkWidget *widget, gpointer data)
{
	image_text_position_cb(widget, data, FOOTER_2);
}

void page_text_position_cb(GtkWidget *widget, gpointer data, TextPosition pos)
{
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) return;

	auto pw = static_cast<PrintWindow *>(data);
	gint new_set = set_toggle(pw->image_group, pos);
	if (new_set >= 0)
		{
		options->printer.image_text_position = static_cast<TextPosition>(new_set);
		}

	options->printer.page_text_position = pos;
}

void page_text_position_h1_cb(GtkWidget *widget, gpointer data)
{
	page_text_position_cb(widget, data, HEADER_1);
}

void page_text_position_h2_cb(GtkWidget *widget, gpointer data)
{
	page_text_position_cb(widget, data, HEADER_2);
}

void page_text_position_f1_cb(GtkWidget *widget, gpointer data)
{
	page_text_position_cb(widget, data, FOOTER_1);
}

void page_text_position_f2_cb(GtkWidget *widget, gpointer data)
{
	page_text_position_cb(widget, data, FOOTER_2);
}

void image_text_template_view_changed_cb(GtkWidget *, gpointer data)
{
	g_free(options->printer.template_string);
	options->printer.template_string = text_widget_text_pull(GTK_WIDGET(data), TRUE);
}

void print_text_menu(GtkWidget *box, PrintWindow *pw)
{
	GtkWidget *group;
	GtkWidget *hbox;
	GtkWidget *button;
	GtkWidget *button1;
	GtkWidget *button2;
	GtkWidget *image_text_button;
	GtkWidget *page_text_button;
	GtkWidget *subgroup;
	GtkWidget *page_text_view;
	GtkWidget *image_text_template_view;
	GtkWidget *scrolled;
	GtkWidget *scrolled_pre_formatted;
	GtkTextBuffer *buffer;

	group = pref_group_new(box, FALSE, _("Image text"), GTK_ORIENTATION_VERTICAL);

	image_text_button = pref_checkbox_new_int(group, _("Show image text"),
										options->printer.show_image_text, &options->printer.show_image_text);

	subgroup = pref_box_new(group, FALSE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);

	pref_checkbox_link_sensitivity(image_text_button, subgroup);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gq_gtk_box_pack_start(GTK_BOX(subgroup), hbox, FALSE, FALSE, 0);

	/* order is important */
	button1 = pref_radiobutton_new(hbox, nullptr,  _("Header 1"),
							options->printer.image_text_position == HEADER_1,
							G_CALLBACK(image_text_position_h1_cb), pw);
	button1 = pref_radiobutton_new(hbox, button1,  _("Header 2"),
							options->printer.image_text_position == HEADER_2,
							G_CALLBACK(image_text_position_h2_cb), pw);
	button1 = pref_radiobutton_new(hbox, button1, _("Footer 1"),
							options->printer.image_text_position == FOOTER_1,
							G_CALLBACK(image_text_position_f1_cb), pw);
	button1 = pref_radiobutton_new(hbox, button1, _("Footer 2"),
							options->printer.image_text_position == FOOTER_2,
							G_CALLBACK(image_text_position_f2_cb), pw);
	gtk_widget_show(hbox);
	pw->image_group = (gtk_radio_button_get_group(GTK_RADIO_BUTTON(button1)));

	image_text_template_view = gtk_text_view_new();

	scrolled_pre_formatted = osd_new(PRE_FORMATTED_COLUMNS, image_text_template_view);
	gq_gtk_box_pack_start(GTK_BOX(subgroup), scrolled_pre_formatted, FALSE, FALSE, 0);
	gtk_widget_show(scrolled_pre_formatted);
	gtk_widget_show(subgroup);

	gtk_widget_set_tooltip_markup(image_text_template_view,
					_("Extensive formatting options are shown in the Help file"));

	scrolled = gq_gtk_scrolled_window_new(nullptr, nullptr);
	gtk_widget_set_size_request(scrolled, 200, 50);
	gq_gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
									GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gq_gtk_box_pack_start(GTK_BOX(subgroup), scrolled, TRUE, TRUE, 5);
	gtk_widget_show(scrolled);

	gq_gtk_container_add(GTK_WIDGET(scrolled), image_text_template_view);
	gtk_widget_show(image_text_template_view);

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(image_text_template_view));
	if (options->printer.template_string) gtk_text_buffer_set_text(buffer, options->printer.template_string, -1);
	g_signal_connect(G_OBJECT(buffer), "changed",
			 G_CALLBACK(image_text_template_view_changed_cb), image_text_template_view);

	hbox = pref_box_new(subgroup, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_BUTTON_GAP);

	button = pref_button_new(nullptr, GQ_ICON_SELECT_FONT, _("Font"),
				 G_CALLBACK(print_set_font_cb), const_cast<char *>("Image text font"));

	gq_gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	pref_spacer(group, PREF_PAD_GAP);

	group = pref_group_new(box, FALSE, _("Page text"), GTK_ORIENTATION_VERTICAL);

	page_text_button = pref_checkbox_new_int(group, _("Show page text"),
					  options->printer.show_page_text, &options->printer.show_page_text);

	subgroup = pref_box_new(group, FALSE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	pref_checkbox_link_sensitivity(page_text_button, subgroup);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gq_gtk_box_pack_start(GTK_BOX(subgroup), hbox, FALSE, FALSE, 0);

	/* order is important */
	button2 = pref_radiobutton_new(hbox, nullptr, _("Header 1"),
							options->printer.page_text_position == HEADER_1,
							G_CALLBACK(page_text_position_h1_cb), pw);
	button2 = pref_radiobutton_new(hbox, button2,  _("Header 2"),
							options->printer.page_text_position == HEADER_2,
							G_CALLBACK(page_text_position_h2_cb), pw);
	button2 = pref_radiobutton_new(hbox, button2, _("Footer 1"),
							options->printer.page_text_position == FOOTER_1,
							G_CALLBACK(page_text_position_f1_cb), pw);
	button2 = pref_radiobutton_new(hbox, button2, _("Footer 2"),
							options->printer.page_text_position == FOOTER_2,
							G_CALLBACK(page_text_position_f2_cb), pw);
	gtk_widget_show(hbox);
	pw->page_group = (gtk_radio_button_get_group(GTK_RADIO_BUTTON(button2)));

	scrolled = gq_gtk_scrolled_window_new(nullptr, nullptr);
	gtk_widget_set_size_request(scrolled, 50, 50);
	gq_gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gq_gtk_box_pack_start(GTK_BOX(subgroup), scrolled, TRUE, TRUE, 5);
	gtk_widget_show(scrolled);

	page_text_view = gtk_text_view_new();
	pw->page_text = gtk_text_view_get_buffer(GTK_TEXT_VIEW(page_text_view ));
	gtk_text_buffer_set_text(GTK_TEXT_BUFFER(pw->page_text), options->printer.page_text, -1);
	g_object_ref(pw->page_text);

	gtk_widget_set_tooltip_markup(page_text_view, (_("Text shown on each page of a single or multi-page print job")));
	gq_gtk_container_add(GTK_WIDGET(scrolled), page_text_view);
	gtk_widget_show(page_text_view);

	hbox = pref_box_new(subgroup, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_BUTTON_GAP);

	button = pref_button_new(nullptr, GQ_ICON_SELECT_FONT, _("Font"),
				 G_CALLBACK(print_set_font_cb), const_cast<char *>("Page text font"));

	gq_gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);
}

gboolean paginate_cb(GtkPrintOperation *, GtkPrintContext *, gpointer data)
{
	auto pw = static_cast<PrintWindow *>(data);

	return pw->job_render_finished;
}

gchar *form_image_text(const gchar *template_string, FileData *fd, PrintWindow *pw, gint page_nr, gint total)
{
	if (!fd) return nullptr;

	OsdTemplate vars;

	const gchar *window_title = gtk_window_get_title(GTK_WINDOW(pw->parent));
	gchar *delimiter = g_strstr_len(window_title, -1, " - Collection - ");
	if (delimiter)
		{
		g_autofree gchar *collection_name = g_strndup(window_title, delimiter - window_title);
		osd_template_insert(vars, "collection", collection_name);
		}

	osd_template_insert(vars, "number", std::to_string(page_nr + 1).c_str());
	osd_template_insert(vars, "total", std::to_string(total).c_str());
	osd_template_insert(vars, "name", fd->name);
	osd_template_insert(vars, "date", text_from_time(fd->date));

	g_autofree gchar *size_str = text_from_size_abrev(fd->size);
	osd_template_insert(vars, "size", size_str);

	if (fd->pixbuf)
		{
		gint w;
		gint h;
		w = gdk_pixbuf_get_width(fd->pixbuf);
		h = gdk_pixbuf_get_height(fd->pixbuf);

		osd_template_insert(vars, "width", std::to_string(w).c_str());
		osd_template_insert(vars, "height", std::to_string(h).c_str());

		g_autofree gchar *res_str = g_strdup_printf("%d × %d", w, h);
		osd_template_insert(vars, "res", res_str);
 		}
	else
		{
		osd_template_insert(vars, "width", nullptr);
		osd_template_insert(vars, "height", nullptr);
		osd_template_insert(vars, "res", nullptr);
		}

	return image_osd_mkinfo(template_string, fd, vars);
}

gchar *print_get_page_text(const PrintWindow *pw)
{
	GtkTextIter start;
	GtkTextIter end;
	gtk_text_buffer_get_bounds(pw->page_text, &start, &end);

	return gtk_text_buffer_get_text(pw->page_text, &start, &end, FALSE);
}

void draw_page(GtkPrintOperation *, GtkPrintContext *context, gint page_nr, gpointer data)
{
	auto pw = static_cast<PrintWindow *>(data);
	FileData *fd;
	cairo_t *cr;
	gdouble context_width;
	gdouble context_height;
	gdouble pixbuf_image_width;
	gdouble pixbuf_image_height;
	gdouble width_offset;
	gdouble height_offset;
	GdkPixbuf *pixbuf;
	GdkPixbuf *rotated = nullptr;
	PangoLayout *layout_image = nullptr;
	PangoLayout *layout_page = nullptr;
	gdouble w;
	gdouble h;
	gdouble scale;
	gdouble image_text_width;
	gdouble page_text_width;
	gint image_y;
	gdouble pango_height;
	gdouble pango_image_height;
	gdouble pango_page_height;

	fd = static_cast<FileData *>(g_list_nth_data(pw->source_selection, page_nr));

	pixbuf = static_cast<GdkPixbuf *>(g_list_nth_data(pw->print_pixbuf_queue, page_nr));
	if (fd->exif_orientation != EXIF_ORIENTATION_TOP_LEFT)
		{
		rotated = pixbuf_apply_orientation(pixbuf, fd->exif_orientation);
		pixbuf = rotated;
		}

	pixbuf_image_width = gdk_pixbuf_get_width(pixbuf);
	pixbuf_image_height = gdk_pixbuf_get_height(pixbuf);

	cr = gtk_print_context_get_cairo_context(context);
	context_width = gtk_print_context_get_width(context);
	context_height = gtk_print_context_get_height(context);

	const auto create_layout = [cr](const gchar *text, const gchar *font, gdouble &text_width, gdouble &pango_height) -> PangoLayout *
	{
		if (!text) return nullptr;

		size_t text_len = strlen(text);
		if (text_len == 0) return nullptr;

		PangoLayout *layout = pango_cairo_create_layout(cr);

		pango_layout_set_text(layout, text, text_len);

		g_autoptr(PangoFontDescription) desc = pango_font_description_from_string(font);
		pango_layout_set_font_description(layout, desc);

		PangoRectangle ink_rect;
		PangoRectangle logical_rect;
		pango_layout_get_extents(layout, &ink_rect, &logical_rect);
		text_width = static_cast<gdouble>(logical_rect.width) / PANGO_SCALE;
		pango_height = static_cast<gdouble>(logical_rect.height) / PANGO_SCALE + PRINT_TEXT_PADDING * 2;

		pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
		pango_layout_set_text(layout, text, text_len);

		return layout;
	};

	pango_image_height = 0;
	pango_page_height = 0;
	image_text_width = 0;
	page_text_width = 0;

	if (options->printer.show_image_text)
		{
		gint total = g_list_length(pw->source_selection);
		g_autofree gchar *image_text = form_image_text(options->printer.template_string, fd, pw, page_nr, total);

		layout_image = create_layout(image_text, options->printer.image_font, image_text_width, pango_image_height);
		}

	if (options->printer.show_page_text)
		{
		g_autofree gchar *page_text = print_get_page_text(pw);

		layout_page = create_layout(page_text, options->printer.page_font, page_text_width, pango_page_height);
		}

	pango_height = pango_image_height + pango_page_height;

	if ((context_width / pixbuf_image_width) < ((context_height - pango_height) / pixbuf_image_height))
		{
		w = context_width;
		scale = context_width / pixbuf_image_width;
		h = pixbuf_image_height * scale;
		height_offset = (context_height - (h + pango_height)) / 2;
		width_offset = 0;
		}
	else
		{
		h = context_height - pango_height ;
		scale = (context_height - pango_height) / pixbuf_image_height;
		w = pixbuf_image_width * scale;
		height_offset = 0;
		width_offset = (context_width - (pixbuf_image_width * scale)) / 2;
		}

	image_y = height_offset;

	if (layout_page)
		{
		gint incr_y = height_offset;

		if (layout_image && options->printer.page_text_position < options->printer.image_text_position)
			{
			incr_y += pango_image_height;
			}

		if (options->printer.page_text_position < HEADER_2)
			{
			incr_y += h;
			}
		else
			{
			image_y += pango_page_height;
			}

		if (options->printer.page_text_position == FOOTER_1)
			{
			incr_y += PRINT_TEXT_PADDING;
			}

		cairo_move_to(cr, (w / 2) - (page_text_width / 2) + width_offset, incr_y);
		pango_cairo_show_layout(cr, layout_page);
		}

	if (layout_image)
		{
		gint incr_y = height_offset;

		if (layout_page && options->printer.image_text_position <= options->printer.page_text_position)
			{
			incr_y += pango_page_height;
			}

		if (options->printer.image_text_position < HEADER_2)
			{
			incr_y += h;
			}
		else
			{
			image_y += pango_image_height;
			}

		if (options->printer.image_text_position == HEADER_1)
			{
			incr_y += PRINT_TEXT_PADDING;
			}

		cairo_move_to(cr, (w / 2) - (image_text_width / 2) + width_offset, incr_y);
		pango_cairo_show_layout(cr, layout_image);
		}

	cairo_scale(cr, scale, scale);

	cairo_rectangle(cr,  width_offset * scale , image_y, pixbuf_image_width / scale, pixbuf_image_height / scale);
	gdk_cairo_set_source_pixbuf(cr, pixbuf, width_offset / scale, image_y / scale);
	cairo_fill(cr);

	if (layout_image) g_object_unref(layout_image);
	if (layout_page) g_object_unref(layout_page);
	if (rotated) g_object_unref(rotated);
}

void begin_print(GtkPrintOperation *operation, GtkPrintContext *, gpointer user_data)
{
	auto pw = static_cast<PrintWindow *>(user_data);
	gint page_count;

	page_count = print_layout_page_count(pw);
	gtk_print_operation_set_n_pages (operation, page_count);

	print_job_render_image(pw);
}


GObject *option_tab_cb(GtkPrintOperation *, gpointer user_data)
{
	auto pw = static_cast<PrintWindow *>(user_data);

	return G_OBJECT(pw->vbox);
}

void end_print_cb(GtkPrintOperation *operation, GtkPrintContext *, gpointer data)
{
	auto pw = static_cast<PrintWindow *>(data);
	GList *work;
	GdkPixbuf *pixbuf;
	GtkPrintSettings *print_settings;
	GtkPageSetup *page_setup;
	GError *error = nullptr;

	print_settings = gtk_print_operation_get_print_settings(operation);
	g_autofree gchar *print_settings_path = g_build_filename(get_rc_dir(), PRINT_SETTINGS, NULL);

	gtk_print_settings_to_file(print_settings, print_settings_path, &error);
	if (error)
		{
		log_printf("Error: Print settings save failed:\n%s", error->message);
		g_error_free(error);
		error = nullptr;
		}
	g_object_unref(print_settings);

	page_setup = gtk_print_operation_get_default_page_setup(operation);
	g_autofree gchar *page_setup_path = g_build_filename(get_rc_dir(), PAGE_SETUP, NULL);

	gtk_page_setup_to_file(page_setup, page_setup_path, &error);
	if (error)
		{
		log_printf("Error: Print page setup save failed:\n%s", error->message);
		g_error_free(error);
		error = nullptr;
		}
	g_object_unref(page_setup);

	g_free(options->printer.page_text);
	options->printer.page_text = print_get_page_text(pw);

	work = pw->print_pixbuf_queue;
	while (work)
		{
		pixbuf = static_cast<GdkPixbuf *>(work->data);
		if (pixbuf)
			{
			g_object_unref(pixbuf);
			}
		work = work->next;
		}
	g_list_free(pw->print_pixbuf_queue);
	g_object_unref(pw->page_text);
	g_free(pw);
}

void print_response_cb(GtkDialog *dialog, gint, gpointer)
{
	gq_gtk_widget_destroy(GTK_WIDGET(dialog));
}

} // namespace

/**
 * @brief Do not free selection, the print window takes control of it
 */
void print_window_new(GList *selection, GtkWidget *parent)
{
	GtkWidget *vbox;
	GtkPrintOperation *operation;
	GtkPageSetup *page_setup;
	const gchar *dir;
	GError *error = nullptr;
	GtkPrintSettings *settings;

	auto pw = g_new0(PrintWindow, 1);

	pw->source_selection = file_data_process_groups_in_selection(selection, FALSE, nullptr);

	if (print_layout_page_count(pw) == 0)
		{
		return;
		}

	pw->parent = parent;

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), PREF_PAD_BORDER);
	gtk_widget_show(vbox);

	print_text_menu(vbox, pw);
	pw->vbox = vbox;

	pw->print_pixbuf_queue = nullptr;
	pw->job_render_finished = FALSE;
	pw->job_page = 0;

	operation = gtk_print_operation_new();
	settings = gtk_print_settings_new();

	gtk_print_operation_set_custom_tab_label(operation, _("Options"));
	gtk_print_operation_set_use_full_page(operation, TRUE);
	gtk_print_operation_set_unit(operation, GTK_UNIT_POINTS);
	gtk_print_operation_set_embed_page_setup(operation, TRUE);
	gtk_print_operation_set_allow_async (operation, TRUE);
	dir = g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS);
	if (dir == nullptr)
		{
		dir = g_get_home_dir();
		}

	g_autofree gchar *uri = g_build_filename("file:/", dir, "geeqie-file.pdf", NULL);
	gtk_print_settings_set(settings, GTK_PRINT_SETTINGS_OUTPUT_URI, uri);

	g_autofree gchar *print_settings_path = g_build_filename(get_rc_dir(), PRINT_SETTINGS, NULL);
	gtk_print_settings_load_file(settings, print_settings_path, &error);
	if (error)
		{
		log_printf("Error: Printer settings load failed:\n%s", error->message);
		g_error_free(error);
		error = nullptr;
		}
	gtk_print_operation_set_print_settings(operation, settings);

	page_setup = gtk_page_setup_new();
	g_autofree gchar *page_setup_path = g_build_filename(get_rc_dir(), PAGE_SETUP, NULL);
	gtk_page_setup_load_file(page_setup, page_setup_path, &error);
	if (error)
		{
		log_printf("Error: Print page setup load failed:\n%s", error->message);
		g_error_free(error);
		error = nullptr;
		}
	gtk_print_operation_set_default_page_setup(operation, page_setup);

	g_signal_connect (G_OBJECT (operation), "begin-print",
					G_CALLBACK (begin_print), pw);
	g_signal_connect (G_OBJECT (operation), "draw-page",
					G_CALLBACK (draw_page), pw);
	g_signal_connect (G_OBJECT (operation), "end-print",
					G_CALLBACK (end_print_cb), pw);
	g_signal_connect (G_OBJECT (operation), "create-custom-widget",
					G_CALLBACK (option_tab_cb), pw);
	g_signal_connect (G_OBJECT (operation), "paginate",
					G_CALLBACK (paginate_cb), pw);

	gtk_print_operation_set_n_pages(operation, print_layout_page_count(pw));

	gtk_print_operation_run(operation, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
												GTK_WINDOW (parent), &error);

	if (error)
		{
		GtkWidget *dialog;

		dialog = gtk_message_dialog_new(GTK_WINDOW (parent),
								GTK_DIALOG_DESTROY_WITH_PARENT,
								GTK_MESSAGE_ERROR,
								GTK_BUTTONS_CLOSE,
								"%s", error->message);
		g_error_free (error);

		g_signal_connect(dialog, "response", G_CALLBACK(print_response_cb), NULL);

		gtk_widget_show (dialog);
		}

	g_object_unref(page_setup);
	g_object_unref(settings);
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
