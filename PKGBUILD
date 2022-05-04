# Maintainer: Vladislav Nepogodin <nepogodin.vlad@gmail.com>

pkgname=cachyos-packageinstaller
pkgver=1.0.0
pkgrel=1
pkgdesc="Simple Software Application Package Installer for CachyOS which helps setting up & installing applications"
arch=(aarch64 x86_64 x86_64_v3)
url="https://github.com/cachyos/kernel-manager"
license=(GPLv3)
depends=('qt5-base' 'polkit-qt5')
makedepends=('cmake' 'ninja' 'git')
groups=('cachyos')
source=("kernel-manager-$pkgver.tar.gz::$url/archive/v$pkgver.tar.gz")
sha256sums=('984ea259546f97e52c85aebecaf921afe89fb603a61856a85ea35bb581c4d65c')
provides=('cachyos-kernel-manager')
conflicts=('cachyos-kernel-manager')
options=(strip)

build() {
  cd ${srcdir}/kernel-manager-${pkgver}

  _cpuCount=$(grep -c -w ^processor /proc/cpuinfo)

  cmake -S . -Bbuild \
        -GNinja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_INSTALL_LIBDIR=lib
  cmake --build build --parallel $_cpuCount
}

package() {
  cd ${srcdir}/kernel-manager-${pkgver}
  DESTDIR="${pkgdir}" cmake --build build --target install

  install -Dm644 LICENSE "${pkgdir}/usr/share/licenses/${pkgname}/LICENSE"
}

# vim:set sw=2 sts=2 et:
