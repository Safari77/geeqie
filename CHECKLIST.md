# Checklist for code updates and new releases of Geeqie

## Code Updates

### Before compiling the sources, carry out the following actions when necessary

* Update `org.geeqie.Geeqie.appdata.xml.in` with the latest released version and date

* Update the desktop template if menus have changed

```sh
./scripts/template-desktop.sh
```

* The command line completion file has three sections that must be updated by hand

```sh
file_types
actions
options

./auto-complete/geeqie
```

### After compiling the sources, carry out the following actions when necessary

* Update the man page and Command Line Options section in Help if the command line options have changed

```sh
./scripts/generate-man-page.sh
```

* Update the keyboard shortcuts page in Help if any keyboard shortcuts have changed

```sh
./doc/create-shortcuts-xml.sh
```

* Commit the changes and push to the .repo

* Update the web-page Help files if they have changed
    * commit and push if necessary

```sh
./scripts/web-help.sh
```

* Copy `org.geeqie.Geeqie.desktop` to `<location of local geeqie.github.io>/`
* Copy `org.geeqie.Geeqie.appdata.xml` to `<location of local geeqie.github.io>/`
* Push changes to `geeqie.github.io`

## New release

Carry out the above actions to ensure the master branch is up to date, and then the following actions for new version \<n.m\>.

* Edit `org.geeqie.Geeqie.appdata.xml.in` - Change date and version
* Edit `NEWS` - The usual information. Ensure the first line is of the form `Geeqie <n.m[.p]>`

```sh
./scripts/new-release.sh <-h for list of options>
```

* Go to `https://github.com/BestImageViewer/geeqie/releases` and click on `Draft a new release`.

* Under `Release title` insert "Geeqie \<n.m\>"

* Under `Choose a tag` select `v<n.m>`

* In `Describe this release` copy-paste the relevant section of `NEWS`.

* Upload the `/tmp/geeqie-<n.m>.tar.xz` and `/tmp/geeqie-<n.m>.tar.xz.asc` files created above

* Click `Publish release`

* Update the [Wikipedia entry](https://en.wikipedia.org/wiki/Geeqie)
