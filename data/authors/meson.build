# SPDX-License-Identifier: GPL-2.0-or-later

authors_sh = join_paths(buildauxdir, 'authors.sh')

authors_resources = custom_target('authors_resources',
                output : 'authors',
                command : [authors_sh, meson.current_build_dir(), running_from_git.to_string()])

project_sources += gnome.compile_resources('authors', 'authors.gresource.xml', dependencies : authors_resources)
