# cachyos-packageinstaller
Simple Software Application Package Installer.

Requirements
------------
* C++23 feature required (tested with GCC 14.1.1 and Clang 18)
Any compiler which support C++23 standard should work.

######
## Installing from source

This is tested on Arch Linux, but *any* recent Arch Linux based system with latest C++23 compiler should do:

```sh
sudo pacman -Sy \
    base-devel cmake pkg-config make qt6-base qt6-tools polkit-qt6
```

### Cloning the source code
```sh
git clone https://github.com/cachyos/packageinstaller.git
cd packageinstaller
```

### Building and Configuring
To build, first, configure it(if you intend to install it globally, you
might also want `--prefix=/usr`):
```sh
./configure.sh --prefix=/usr/local
```
Second, build it:
```sh
./build.sh
```

### Easy way to verify pkglist.yaml in fish
```fish
for pkg in (yq -r '.[].packages[]' pkglist.yaml)
    for split in (string split ' ' $pkg)
        pacman -Si $split >/dev/null 2>&1; and echo "Found: $split"; or echo "NOT FOUND: $split"
    end
end
```

### Libraries used in this project

* [Qt](https://www.qt.io) used for GUI.
* [A modern formatting library](https://github.com/fmtlib/fmt) used for formatting strings, output and logging.

