# Third-party material

## Libraries

Fetched at pinned tags through CPM at configure time (see `CMakeLists.txt`) and linked into
the binaries.

| Library | Used for | License |
| - | - | - |
| [rapidyaml](https://github.com/biojppm/rapidyaml) v0.16.0 | reading `pkglist.yaml` | MIT |
| [backward-cpp](https://github.com/bombela/backward-cpp) v1.6 | stack traces on crashes (GUI and `cachyos-pi-scan`) | MIT |

## Colour palettes

The settings page offers colour themes based on the community palettes below. The
colour values in `qml/Look.qml` are taken from these projects; some shades are blends
of them. The projects are not affiliated with CachyOS and do not endorse this
application. All five are published under the MIT License.

| Theme | Source | Copyright |
| --- | --- | --- |
| Nord | <https://github.com/nordtheme/nord> | Copyright (c) 2016-present Sven Greb |
| Dracula (light: Alucard) | <https://github.com/dracula/dracula-theme> | Copyright (c) 2023 Dracula Theme |
| Catppuccin (Mocha and Latte) | <https://github.com/catppuccin/catppuccin> | Copyright (c) 2021 Catppuccin |
| Tokyo Night | <https://github.com/tokyo-night/tokyo-night-vscode-theme> | Copyright (c) 2018-present Enkia |
| Solarized | <https://github.com/altercation/solarized> | Copyright (c) 2011 Ethan Schoonover |

### MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
