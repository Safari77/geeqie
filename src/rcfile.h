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

#ifndef RCFILE_H
#define RCFILE_H

#include <gdk/gdk.h>
#include <glib.h>

struct ConfOptions;
struct LayoutWindow;

void write_indent(GString *str, gint indent);
void write_char_option(GString *str, const gchar *label, const gchar *text);
gboolean read_dummy_option(const gchar *option, const gchar *label, const gchar *message);
gboolean read_char_option(const gchar *option, const gchar *label, const gchar *value, gchar **text);
void write_color_option(GString *str, const gchar *label, const GdkRGBA *color);
gboolean read_color_option(const gchar *option, const gchar *label, const gchar *value, GdkRGBA *color);
void write_int_option(GString *str, const gchar *label, gint n);
gboolean read_int_option(const gchar *option, const gchar *label, const gchar *value, gint *n);
gboolean read_ushort_option(const gchar *option, const gchar *label, const gchar *value, guint16 *n);
void write_uint_option(GString *str, const gchar *label, guint n);
gboolean read_uint_option(const gchar *option, const gchar *label, const gchar *value, guint *n);
gboolean read_uint_option_clamp(const gchar *option, const gchar *label, const gchar *value, guint *n, guint min, guint max);
gboolean read_int_option_clamp(const gchar *option, const gchar *label, const gchar *value, gint *n, gint min, gint max);
void write_int_unit_option(GString *str, const gchar *label, gint n, gint subunits);
gboolean read_int_unit_option(const gchar *option, const gchar *label, const gchar *value, gint *n, gint subunits);
void write_bool_option(GString *str, const gchar *label, gboolean value);
gboolean read_bool_option(const gchar *option, const gchar *label, const gchar *value, gint *n);

#define WRITE_BOOL(_source_, _name_) write_bool_option(outstr, #_name_, (_source_)._name_)
#define WRITE_INT(_source_, _name_) write_int_option(outstr, #_name_, (_source_)._name_)
#define WRITE_UINT(_source_, _name_) write_uint_option(outstr, #_name_, (_source_)._name_)
#define WRITE_INT_UNIT(_source_, _name_, _unit_) write_int_unit_option(outstr, #_name_, (_source_)._name_, _unit_)
#define WRITE_CHAR(_source_, _name_) write_char_option(outstr, #_name_, (_source_)._name_)
#define WRITE_COLOR(_source_, _name_) write_color_option(outstr, #_name_, &(_source_)._name_)

#define WRITE_INT_FULL(_name_, _source_) write_int_option(outstr, _name_, _source_)

#define WRITE_NL() write_indent(outstr, indent)
#define WRITE_SEPARATOR() g_string_append(outstr, "\n")
#define WRITE_STRING(_str_) g_string_append(outstr, _str_)
#define WRITE_FORMAT_STRING(...) g_string_append_printf(outstr, __VA_ARGS__)

#define READ_BOOL(_target_, _name_) read_bool_option(option, #_name_, value, &(_target_)._name_)
#define READ_INT(_target_, _name_) read_int_option(option, #_name_, value, &(_target_)._name_)
#define READ_INT_ENUM(_target_, _name_) read_int_option(option, #_name_, value, (gint*)&(_target_)._name_)
#define READ_UINT(_target_, _name_) read_uint_option(option, #_name_, value, &(_target_)._name_)
#define READ_UINT_ENUM(_target_, _name_) read_uint_option(option, #_name_, value, (guint*)&(_target_)._name_)
#define READ_USHORT(_target_, _name_) read_ushort_option(option, #_name_, value, &(_target_)._name_)
#define READ_USHORT_FULL(_name_, _target_) read_ushort_option(option, _name_, value, &(_target_))
#define READ_INT_CLAMP(_target_, _name_, _min_, _max_) read_int_option_clamp(option, #_name_, value, &(_target_)._name_, _min_, _max_)
#define READ_UINT_CLAMP(_target_, _name_, _min_, _max_) read_uint_option_clamp(option, #_name_, value, &(_target_)._name_, _min_, _max_)
#define READ_UINT_ENUM_CLAMP(_target_, _name_, _min_, _max_) read_uint_option_clamp(option, #_name_, value, (guint*)&(_target_)._name_, _min_, _max_)
#define READ_INT_UNIT(_target_, _name_, _unit_) read_int_unit_option(option, #_name_, value, &(_target_)._name_, _unit_)
#define READ_CHAR(_target_, _name_) read_char_option(option, #_name_, value, &(_target_)._name_)
#define READ_COLOR(_target_, _name_) read_color_option(option, #_name_, value, &(_target_)._name_)

#define READ_BOOL_FULL(_name_, _target_) read_bool_option(option, _name_, value, &(_target_))
#define READ_INT_FULL(_name_, _target_) read_int_option(option, _name_, value, &(_target_))
#define READ_UINT_FULL(_name_, _target_) read_uint_option(option, _name_, value, &(_target_))
#define READ_INT_CLAMP_FULL(_name_, _target_, _min_, _max_) read_int_option_clamp(option, _name_, value, &(_target_), _min_, _max_)
#define READ_INT_UNIT_FULL(_name_, _target_, _unit_) read_int_unit_option(option, _name_, value, &(_target_), _unit_)
#define READ_CHAR_FULL(_name_, _target_) read_char_option(option, _name_, value, &(_target_))
#define READ_COLOR_FULL(_name_, _target_) read_color_option(option, _name_, value, &(_target_))

#define READ_DUMMY(_target_, _name_, _msg_) read_dummy_option(option, #_name_, _msg_)


gboolean save_config_to_file(const gchar *utf8_path, ConfOptions *options, LayoutWindow *lw);
gboolean save_default_layout_options_to_file(const gchar *utf8_path, LayoutWindow *lw);

gboolean load_config_from_buf(const gchar *buf, gsize size, gboolean startup);
gboolean load_config_from_file(const gchar *utf8_path, gboolean startup);

void config_file_error(const gchar *message);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
