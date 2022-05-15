# Maintainer: Vladislav Nepogodin <nepogodin.vlad@gmail.com>

pkgname=cachyos-packageinstaller
pkgver=1.0.3
pkgrel=1
pkgdesc="Simple Software Application Package Installer for CachyOS which helps setting up & installing applications"
arch=(aarch64 x86_64 x86_64_v3)
url="https://github.com/cachyos/packageinstaller"
license=(GPLv3)
depends=('qt5-base' 'polkit-qt5')
makedepends=('cmake' 'ninja' 'git')
groups=('cachyos')
source=("packageinstaller-$pkgver.tar.gz::$url/archive/v$pkgver.tar.gz")
sha256sums=('40cbed6b1ace79c7ffbeca684588cb9a96d24e84190fd33119fc44754aae1633')
provides=('cachyos-packageinstaller')
conflicts=('cachyos-packageinstaller')
options=(strip)

build() {
  cd ${srcdir}/packageinstaller-${pkgver}

  _cpuCount=$(grep -c -w ^processor /proc/cpuinfo)

  cmake -S . -Bbuild \
        -GNinja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_INSTALL_LIBDIR=lib
  cmake --build build --parallel $_cpuCount
}

package() {
  cd ${srcdir}/packageinstaller-${pkgver}
  DESTDIR="${pkgdir}" cmake --build build --target install

  install -Dm644 LICENSE "${pkgdir}/usr/share/licenses/${pkgname}/LICENSE"
}

# vim:set sw=2 sts=2 et:
