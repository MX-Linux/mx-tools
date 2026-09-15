# Maintainer: Adrian <adrian@mxlinux.org>
pkgname=mx-tools
pkgver=${PKGVER:-25.4}
pkgrel=1
pkgdesc="MX Tools - Dashboard application launcher for various MX tools"
arch=('x86_64' 'i686')
url="https://mxlinux.org"
license=('GPL3')
depends=('qt6-base' 'qt6-declarative')
makedepends=('cmake' 'ninja' 'qt6-declarative' 'qt6-tools')
source=()
sha256sums=()

build() {
    cd "${startdir}"

    # Clean any previous build artifacts
    rm -rf build

    # Configure with CMake
    cmake -G Ninja \
        -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DPROJECT_VERSION_OVERRIDE="${pkgver}"

    # Build
    cmake --build build --parallel
}

package() {
    cd "${startdir}"

    # Install binary
    install -Dm755 build/mx-tools "${pkgdir}/usr/bin/mx-tools"

    # Install translations
    install -dm755 "${pkgdir}/usr/share/mx-tools/locale"
    install -Dm644 -t "${pkgdir}/usr/share/mx-tools/locale/" build/*.qm 2>/dev/null || true

    # Install desktop file
    install -Dm644 data/mx-tools.desktop "${pkgdir}/usr/share/applications/mx-tools.desktop"

    # Install the "MX Tools" submenu. Arch only: on Debian these come from
    # desktop-defaults-mx-common. The file names differ from that package's so
    # both can be installed without a pacman file conflict - menus sharing a
    # <Name> are merged and their include rules accumulate.
    install -Dm644 data/menu/mx-tools-menu.directory \
        "${pkgdir}/usr/share/desktop-directories/mx-tools-menu.directory"
    install -Dm644 data/menu/mx-tools-menu.menu \
        "${pkgdir}/etc/xdg/menus/applications-merged/mx-tools-menu.menu"

    # Install icons
    install -Dm644 icons/mx-tools.png "${pkgdir}/usr/share/icons/hicolor/96x96/apps/mx-tools.png"
    install -Dm644 icons/mx-tools.svg "${pkgdir}/usr/share/icons/hicolor/scalable/apps/mx-tools.svg"

    # Install documentation
    install -dm755 "${pkgdir}/usr/share/doc/mx-tools"

    install -dm755 "${pkgdir}/usr/share/man/man1"
    install -Dm644 -t "${pkgdir}/usr/share/man/man1/" help/*.1 2>/dev/null || true
    if [ -d help ]; then
        for help_file in help/*.html help/*.jpg help/*.png help/*.css; do
            [ -f "$help_file" ] && install -Dm644 "$help_file" "${pkgdir}/usr/share/doc/mx-tools/$(basename "$help_file")"
        done
    fi

    # Install license
    install -Dm644 LICENSE "${pkgdir}/usr/share/licenses/${pkgname}/LICENSE"

    # Install changelog
    install -Dm644 <(gzip -c debian/changelog) "${pkgdir}/usr/share/doc/${pkgname}/changelog.gz"
}
