# Maintainer: Adrian <adrian@mxlinux.org>
pkgname=mx-tools
pkgver=${PKGVER:-25.4}
pkgrel=1
pkgdesc="MX Tools - Dashboard application for configuration tools in MX Linux"
arch=('x86_64' 'i686')
url="https://mxlinux.org"
license=('GPL3')
depends=('qt6-base')
makedepends=('cmake' 'ninja' 'qt6-tools')
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
    install -Dm644 build/*.qm "${pkgdir}/usr/share/mx-tools/locale/" 2>/dev/null || true

    # Install desktop file
    install -Dm644 mx-tools.desktop "${pkgdir}/usr/share/applications/mx-tools.desktop"

    # Install icons
    install -Dm644 icons/mx-tools.png "${pkgdir}/usr/share/icons/hicolor/96x96/apps/mx-tools.png"
    install -Dm644 icons/mx-tools.svg "${pkgdir}/usr/share/icons/hicolor/scalable/apps/mx-tools.svg"

    # Install documentation
    install -dm755 "${pkgdir}/usr/share/doc/mx-tools"
    if [ -d help ]; then
        cp -r help/* "${pkgdir}/usr/share/doc/mx-tools/" 2>/dev/null || true
    fi

    # Install license
    install -Dm644 LICENSE "${pkgdir}/usr/share/licenses/${pkgname}/LICENSE"

    # Install changelog
    install -Dm644 <(gzip -c debian/changelog) "${pkgdir}/usr/share/doc/${pkgname}/changelog.gz"
}
