# cachyos-packageinstaller
Simple Software Application Package Installer.

Requirements
------------
* C++20 feature required (tested with GCC 11.1.0 and Clang 13(clang will not compile it with libstdc++ 11.1.0 because of c++20 standard ranges library)
Any compiler which support C++20 standard should work.

######
## Installing from source

This is tested on Arch Linux, but *any* recent Arch Linux based system with latest C++20 compiler should do:

```sh
sudo pacman -Sy \
    base-devel cmake pkg-config make qt5-base
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


### Libraries used in this project

* [Qt](https://www.qt.io) used for GUI.
* [A modern formatting library](https://github.com/fmtlib/fmt) used for formatting strings, output and logging.
* [Ranges](https://github.com/ericniebler/range-v3) used for ranges support with clang.

