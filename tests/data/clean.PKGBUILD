# Maintainer: Somebody <somebody at example dot org>
pkgname=hello-tool
pkgver=1.4.2
pkgrel=1
pkgdesc="Small tool that uses curl and wget style downloads, with eval support"
arch=('x86_64')
url="https://github.com/example/hello-tool"
license=('MIT')
depends=('curl' 'glibc')
makedepends=('git' 'cmake' 'wget' 'sudo')
install=$pkgname.install
source=("$pkgname-$pkgver.tar.gz::https://github.com/example/hello-tool/archive/v$pkgver.tar.gz"
        "hello-tool.service"
        "fix-build.patch")
sha256sums=('0f3a5f5b0a9c2f6b8c0d51b7d9a2e5f2d3b1c0a9e8f7d6c5b4a39281706f5e4d'
            'b5bb9d8014a0f9b1d61e21e796d78dccdf1352f23cd32812f4850b878ae4944c'
            '7d865e959b2466918c9863afca942d0fb89d7c9ac0c99bafc3749504ded97730')

prepare() {
  cd "$pkgname-$pkgver"
  patch -Np1 -i "$srcdir/fix-build.patch"
  export HOME="$srcdir/home"
  export CARGO_HOME="$srcdir/cargo"
}

build() {
  cmake -B build -S "$pkgname-$pkgver" \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DUSE_CURL=ON --with-curl
  cmake --build build
}

package() {
  depends+=('wget')
  DESTDIR="$pkgdir" cmake --install build
  install -Dm644 "$srcdir/hello-tool.service" "$pkgdir/usr/lib/systemd/system/hello-tool.service"
  install -Dm644 "$pkgname-$pkgver/LICENSE" "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
  install -Dm755 /dev/stdin "$pkgdir/etc/profile.d/hello.sh" <<< 'export HELLO=1'
  ln -s /usr/lib/hello-tool/hello "$pkgdir/usr/bin/hello"
  chmod 755 "$pkgdir/usr/bin/hello"
}
