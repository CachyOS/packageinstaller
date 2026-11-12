# cachyos-packageinstaller

A small app store for CachyOS: browse, install, remove and update packages from
the CachyOS and Arch repositories, and build packages from the AUR.

- **Repositories** are read directly through libalpm; nothing runs as root until
  you confirm an operation.
- **Icons and app metadata** come from the system AppStream catalog
  (`archlinux-appstream-data`, and any other catalog in `/usr/share/swcatalog`).
  Packages without AppStream data fall back to the icon theme and then to a
  generated tile, so every package has an icon.
- **AUR** is opt-in. Search and PKGBUILD review happen in the app; building is
  delegated to `paru`. A package is cloned for the review, and that checkout is
  what gets built, so a later change in the AUR cannot slip in. AUR packages it
  depends on are reviewed in the same way; paru is started so that it cannot
  fetch anything else from the AUR. Once enabled, the AUR page lists the installed AUR
  packages that have a newer version. Each update is reviewed like an install;
  "Review and update all" walks through the reviews and builds the accepted
  packages in one go.
- **PKGBUILD check**: the review page marks lines a reviewer should look at
  (see below). It is a reading aid and never declares a package safe.
- **Privileges**: the GUI runs as the user. Package operations go through
  `cachyos-pi-helper`, a tiny pkexec helper that only accepts
  `install`, `remove` and `upgrade`.

## Screenshots

| | |
| --- | --- |
| ![Start page with the CachyOS picks](screenshots/discover.png) | ![Package details with AppStream screenshots](screenshots/details.png) |
| Start page | Package details |
| ![AUR review page with the findings of the PKGBUILD check](screenshots/aur-review.png) | ![AUR page listing updates for installed AUR packages](screenshots/aur-updates.png) |
| PKGBUILD review with the automatic check | Updates for AUR packages |

## Dependencies

Build:
```sh
sudo pacman -S base-devel cmake ninja qt6-base qt6-declarative qt6-tools appstream-qt pacman
```

Runtime: `qt6-declarative`, `appstream-qt`, `archlinux-appstream-data`, `polkit`,
`kimageformats` (the Arch icon cache is JPEG XL). Optional: `paru` and `git` (AUR review and builds),
`pacman-contrib` (`checkupdates`, to see updates without refreshing the system
databases).

## Building

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
sudo cmake --install build
```

An uninstalled build can be run as `build/cachyos-pi`; without the installed
helper it falls back to `pkexec pacman`.

Run the tests with `ctest --test-dir build`.

## Packaging

`packaging/` holds two PKGBUILDs: `cachyos-packageinstaller` builds a tagged
release, `cachyos-packageinstaller-git` the default branch. The release one
needs its checksum filled in (`updpkgsums`) once the version is tagged.

```sh
cd packaging/cachyos-packageinstaller-git
makepkg -si
```

## PKGBUILD check

`src/pkgscan.*` looks at the PKGBUILD, the install scriptlet and other scripts
shipped with an AUR package, plus the package's AUR record. It only reads text:
nothing from the package is sourced or executed. Findings have three levels:

| Level | Examples |
| --- | --- |
| Dangerous | download piped into a shell, decoded-and-executed text, reverse shells, access to SSH keys or browser data, edits to sudoers |
| Check | downloads without checksum, downloads outside `source=()`, `eval`, `sudo` in a build, writes outside `$pkgdir`, setuid bits, enabled services, orphaned or very new packages, look-alike names |
| Note | unpinned VCS sources, outdated checksum types, commands at the top level, changed maintainer |

The check knows common tricks only. No findings does not mean that a package is
safe, which is why the review checkbox stays for every package.

The same check is available on the command line:

```sh
cachyos-pi-scan path/to/package        # directory with a PKGBUILD
cachyos-pi-scan --json path/to/PKGBUILD
```

It exits with 0 (nothing or notes), 1 (something to check), 2 (dangerous
pattern) or 3 (unreadable). The package record and name checks need the AUR and
the package database, so they are only done in the app.

## Layout

| Path | Contents |
| --- | --- |
| `src/alpmdb.*` | read-only libalpm access |
| `src/catalog.*` | AppStream catalog, merged into the package list |
| `src/iconprovider.*` | `image://pkgicon/<name>` icon lookup |
| `src/packagemodel.*` | list model and search/category filter |
| `src/aurclient.*` | AUR RPC search, details and PKGBUILD download |
| `src/pkgscan.*` | static PKGBUILD and script check |
| `src/scan/` | the `cachyos-pi-scan` command line tool |
| `tests/` | unit tests for the check |
| `src/transaction.*` | plan → confirm → run state machine |
| `src/helper/` | the pkexec helper |
| `qml/` | the user interface |
| `pkglist.yaml` | the curated "CachyOS picks" on the start page |
| `org.cachyos.cachyos-pi.metainfo.xml` | AppStream metadata for software centres |
| `packaging/` | PKGBUILDs |
| `screenshots/` | the pictures in this file |
