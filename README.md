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
  delegated to `paru`.
- **Privileges**: the GUI runs as the user. Package operations go through
  `cachyos-pi-helper`, a tiny pkexec helper that only accepts
  `install`, `remove` and `upgrade`.

## Dependencies

Build:
```sh
sudo pacman -S base-devel cmake ninja qt6-base qt6-declarative appstream-qt pacman
```

Runtime: `qt6-declarative`, `appstream-qt`, `archlinux-appstream-data`, `polkit`,
`kimageformats` (the Arch icon cache is JPEG XL). Optional: `paru` (AUR builds),
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

## Layout

| Path | Contents |
| --- | --- |
| `src/alpmdb.*` | read-only libalpm access |
| `src/catalog.*` | AppStream catalog, merged into the package list |
| `src/iconprovider.*` | `image://pkgicon/<name>` icon lookup |
| `src/packagemodel.*` | list model and search/category filter |
| `src/aurclient.*` | AUR RPC search, details and PKGBUILD download |
| `src/transaction.*` | plan → confirm → run state machine |
| `src/helper/` | the pkexec helper |
| `qml/` | the user interface |
| `pkglist.yaml` | the curated "CachyOS picks" on the start page |
