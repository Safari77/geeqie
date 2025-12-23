# SPDX-License-Identifier: GPL-2.0-or-later

# Install other project files
if running_from_git
    cmd = [find_program('gen-changelog.sh', dirs : buildauxdir), meson.project_source_root(), meson.current_build_dir()]

    changelog_template = files('ChangeLog.gqview')
     custom_target(
        'ChangeLog',
        input: changelog_template,
        output: ['ChangeLog', 'ChangeLog.html'],
        command: cmd,
        install: true,
        install_dir: helpdir)
    meson.add_dist_script(cmd)
    summary({'ChangeLog' : ['ChangeLog, ChangeLog.html created:', true]}, section : 'Documentation', bool_yn : true)
elif fs.exists('ChangeLog.html')
    install_data('ChangeLog', 'ChangeLog.html', install_dir: helpdir)
    summary({'ChangeLog' : ['ChangeLog, ChangeLog.html installed from dist:', true]}, section : 'Documentation', bool_yn : true)
endif
