# SPDX-License-Identifier: GPL-2.0-or-later

project_sources += gnome.compile_resources('icons', 'icons.gresource.xml')

subdir('svg')

install_subdir(
  'hicolor',
  install_dir: join_paths(get_option('datadir'), 'icons')
)
