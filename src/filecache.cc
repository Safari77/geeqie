/*
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: Vladimir Nadvornik
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

#include "filecache.h"

#include "filedata.h"

/* this implements a simple LRU algorithm */

struct FileCacheData {
	FileCacheReleaseFunc release;
	GList *list;
	gulong max_size;
	gulong size;
};

namespace
{

struct FileCacheEntry {
	FileData *fd;
	gulong size;
};

#ifdef DEBUG
constexpr bool debug_file_cache = false; /* Set to true to add file cache dumps to the debug output */

void file_cache_dump(FileCacheData *fc)
{
	if (!debug_file_cache) return;

	DEBUG_1("cache dump: fc=%p max size:%lu size:%lu", (void *)fc, fc->max_size, fc->size);

	gulong n = 0;
	for (GList *work = fc->list; work; work = work->next)
		{
		auto fe = static_cast<FileCacheEntry *>(work->data);
		DEBUG_1("cache entry: fc=%p [%lu] %s %lu", (void *)fc, ++n, fe->fd->path, fe->size);
		}
}
#else
#  define file_cache_dump(fc)
#endif

} // namespace

static void file_cache_notify_cb(FileData *fd, NotifyType type, gpointer data);
static void file_cache_remove_fd(FileCacheData *fc, FileData *fd);

FileCacheData *file_cache_new(FileCacheReleaseFunc release, gulong max_size)
{
	auto fc = g_new(FileCacheData, 1);

	fc->release = release;
	fc->list = nullptr;
	fc->max_size = max_size;
	fc->size = 0;

	file_data_register_notify_func(file_cache_notify_cb, fc, NOTIFY_PRIORITY_HIGH);

	return fc;
}

gboolean file_cache_get(FileCacheData *fc, FileData *fd)
{
	GList *work;

	g_assert(fc && fd);

	work = fc->list;
	while (work)
		{
		auto fce = static_cast<FileCacheEntry *>(work->data);
		if (fce->fd == fd)
			{
			/* entry exists */
			DEBUG_2("cache hit: fc=%p %s", (void *)fc, fd->path);
			if (work == fc->list) return TRUE; /* already at the beginning */
			/* move it to the beginning */
			DEBUG_2("cache move to front: fc=%p %s", (void *)fc, fd->path);
			fc->list = g_list_remove_link(fc->list, work);
			fc->list = g_list_concat(work, fc->list);

			if (file_data_check_changed_files(fd)) {
				/* file has been changed, cance entry is no longer valid */
				file_cache_remove_fd(fc, fd);
				return FALSE;
			}

			file_cache_dump(fc);
			return TRUE;
			}
		work = work->next;
		}
	DEBUG_2("cache miss: fc=%p %s", (void *)fc, fd->path);
	return FALSE;
}

void file_cache_set_size(FileCacheData *fc, gulong size)
{
	GList *work;
	FileCacheEntry *last_fe;

	file_cache_dump(fc);

	work = g_list_last(fc->list);
	while (fc->size > size && work)
		{
		GList *prev;
		last_fe = static_cast<FileCacheEntry *>(work->data);
		prev = work->prev;
		fc->list = g_list_delete_link(fc->list, work);
		work = prev;

		DEBUG_2("file changed - cache remove: fc=%p %s", (void *)fc, last_fe->fd->path);
		fc->size -= last_fe->size;
		fc->release(last_fe->fd);
		file_data_unref(last_fe->fd);
		g_free(last_fe);
		}
}

void file_cache_put(FileCacheData *fc, FileData *fd, gulong size)
{
	FileCacheEntry *fe;

	if (file_cache_get(fc, fd)) return;

	DEBUG_2("cache add: fc=%p %s", (void *)fc, fd->path);
	fe = g_new(FileCacheEntry, 1);
	fe->fd = file_data_ref(fd);
	fe->size = size;
	fc->list = g_list_prepend(fc->list, fe);
	fc->size += size;

	file_cache_set_size(fc, fc->max_size);
}

void file_cache_set_max_size(FileCacheData *fc, gulong size)
{
	fc->max_size = size;
	file_cache_set_size(fc, fc->max_size);
}

static void file_cache_remove_fd(FileCacheData *fc, FileData *fd)
{
	GList *work;
	FileCacheEntry *fe;

	file_cache_dump(fc);

	work = fc->list;
	while (work)
		{
		GList *current = work;
		fe = static_cast<FileCacheEntry *>(work->data);
		work = work->next;

		if (fe->fd == fd)
			{
			fc->list = g_list_delete_link(fc->list, current);

			DEBUG_1("cache remove: fc=%p %s", (void *)fc, fe->fd->path);
			fc->size -= fe->size;
			fc->release(fe->fd);
			file_data_unref(fe->fd);
			g_free(fe);
			}
		}
}

static void file_cache_notify_cb(FileData *fd, NotifyType type, gpointer data)
{
	auto fc = static_cast<FileCacheData *>(data);

	if (type & (NOTIFY_REREAD | NOTIFY_CHANGE)) /* invalidate the entry on each file change */
		{
		DEBUG_1("Notify cache: %s %04x", fd->path, type);
		file_cache_remove_fd(fc, fd);
		}
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
