#!/bin/bash

# **********************************************************************
# * Copyright (C) 2017-2026 MX Authors
# *
# * Authors: Adrian
# *          MX Linux <http://mxlinux.org>
# *
# * This file is part of mx-tools.
# *
# * mx-tools is free software: you can redistribute it and/or modify
# * it under the terms of the GNU General Public License as published by
# * the Free Software Foundation, either version 3 of the License, or
# * (at your option) any later version.
# *
# * mx-tools is distributed in the hope that it will be useful,
# * but WITHOUT ANY WARRANTY; without even the implied warranty of
# * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# * GNU General Public License for more details.
# *
# * You should have received a copy of the GNU General Public License
# * along with mx-tools.  If not, see <http://www.gnu.org/licenses/>.
# **********************************************************************/

set -e

# Default values
BUILD_DIR="build"
BUILD_TYPE="Release"
USE_CLANG=false
CLEAN=false
DEBIAN_BUILD=false
ARCH_BUILD=false

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -d|--debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        -c|--clang)
            USE_CLANG=true
            shift
            ;;
        --clean)
            CLEAN=true
            shift
            ;;
        --debian)
            DEBIAN_BUILD=true
            shift
            ;;
        --arch)
            ARCH_BUILD=true
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo "Options:"
            echo "  -d, --debug     Build in Debug mode (default: Release)"
            echo "  -c, --clang     Use clang compiler"
            echo "  --clean         Clean build directory before building"
            echo "  --debian        Build Debian package"
            echo "  --arch          Build Arch Linux package"
            echo "  -h, --help      Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Build Debian package
if [ "$DEBIAN_BUILD" = true ]; then
    DEBIAN_SOURCE=$(dpkg-parsechangelog -SSource)
    DEBIAN_ARCH=$(dpkg-architecture -qDEB_HOST_ARCH)
    # The .changes, .buildinfo and source artifacts are all named after the
    # source version from debian/changelog. On Trixie, debian/rules gives the
    # binary package an "mx25" suffix via dh_gencontrol, which leaves the
    # source version - and therefore this prefix - untouched.
    DEBIAN_VERSION=$(dpkg-parsechangelog -SVersion)
    DEBIAN_ARTIFACT_PREFIX="${DEBIAN_SOURCE}_${DEBIAN_VERSION#*:}_${DEBIAN_ARCH}"

    # Build from an export of the repository's files, not from the checkout: dpkg-source
    # packs a native package's whole directory, so build trees, editor state or a makepkg
    # pkg/ lying around would end up in the source tarball. debs/, the earlier release
    # artifacts, is left out too. Only files Git knows about are exported (with their
    # uncommitted edits), so git add a new file before building. The export is called src
    # because the tarball's top directory is named after it, and arch/PKGBUILD (OBS)
    # unpacks it from src/.
    DEBIAN_BUILDDIR=$(mktemp -d)
    trap 'rm -rf "$DEBIAN_BUILDDIR"' EXIT
    mkdir "$DEBIAN_BUILDDIR/src"
    git ls-files -z --cached -- . ':!debs' \
        | while IFS= read -r -d '' file; do [ -e "$file" ] && printf '%s\0' "$file"; done \
        | tar --null -T - -c | tar -x -C "$DEBIAN_BUILDDIR/src"

    echo "Building Debian package..."
    (cd "$DEBIAN_BUILDDIR/src" && debuild -us -uc)

    # The current build's manifest lists its binary and source artifacts,
    # including buildinfo. Never sweep unrelated files from the parent directory.
    DEBIAN_CHANGES="$DEBIAN_BUILDDIR/${DEBIAN_ARTIFACT_PREFIX}.changes"
    if [ ! -f "$DEBIAN_CHANGES" ]; then
        echo "Error: expected build manifest not found: $DEBIAN_CHANGES"
        exit 1
    fi
    mapfile -t DEBIAN_ARTIFACTS < <(awk '
        /^Files:$/ { in_files = 1; next }
        in_files && /^[^[:space:]]/ { in_files = 0 }
        in_files && NF == 5 { print $5 }
    ' "$DEBIAN_CHANGES")
    for artifact in "${DEBIAN_ARTIFACTS[@]}"; do
        if [[ "$artifact" == */* || "$artifact" == "." || "$artifact" == ".." || ! -f "$DEBIAN_BUILDDIR/$artifact" ]]; then
            echo "Error: invalid or missing build artifact: $artifact"
            exit 1
        fi
    done

    echo "Creating debs directory and moving debian artifacts..."
    mkdir -p debs
    for artifact in "${DEBIAN_ARTIFACTS[@]}"; do
        mv -- "$DEBIAN_BUILDDIR/$artifact" debs/
    done
    mv -- "$DEBIAN_CHANGES" debs/
    if [ -f "$DEBIAN_BUILDDIR/${DEBIAN_ARTIFACT_PREFIX}.build" ]; then
        mv -- "$DEBIAN_BUILDDIR/${DEBIAN_ARTIFACT_PREFIX}.build" debs/
    fi

    echo "Debian package build completed!"
    echo "Debian artifacts moved to debs/ directory"
    exit 0
fi

# Build Arch Linux package
if [ "$ARCH_BUILD" = true ]; then
    echo "Building Arch Linux package..."

    if ! command -v makepkg &> /dev/null; then
        echo "Error: makepkg not found. Please install base-devel package."
        exit 1
    fi

    if [ ! -f debian/changelog ]; then
        echo "Error: debian/changelog not found; cannot determine version for Arch build."
        exit 1
    fi

    ARCH_VERSION=$(sed -n '1{s/^[^(]*(\([^)]*\)).*/\1/p}' debian/changelog)
    if [ -z "$ARCH_VERSION" ]; then
        echo "Error: could not parse version from debian/changelog."
        exit 1
    fi
    echo "Using version ${ARCH_VERSION} from debian/changelog"

    # Build arch/PKGBUILD - the same file OBS uses - from a tarball of the working tree,
    # in a scratch directory under build/, so makepkg's src/ and pkg/ never land in the
    # repository root or meet its own src/. The tarball unpacks to src/, as the
    # dpkg-source one OBS gets does. Only files Git knows about are packed (with their
    # uncommitted edits), so git add a new file before building.
    mkdir -p build
    ARCH_BUILDDIR=$(mktemp -d -p "$PWD/build" arch.XXXXXX)
    trap 'rm -rf "$ARCH_BUILDDIR"' EXIT
    git ls-files -z --cached -- . ':!debs' \
        | while IFS= read -r -d '' file; do [ -e "$file" ] && printf '%s\0' "$file"; done \
        | tar --null -T - --transform 's,^,src/,' -cJf "$ARCH_BUILDDIR/mx-tools_${ARCH_VERSION}.tar.xz"
    sed "s/^pkgver=.*/pkgver=${ARCH_VERSION}/" arch/PKGBUILD > "$ARCH_BUILDDIR/PKGBUILD"

    PKG_DEST_DIR="$PWD/build"
    (cd "$ARCH_BUILDDIR" && PKGDEST="$PKG_DEST_DIR" makepkg -f)

    echo "Arch Linux package build completed!"
    echo "Package: $(ls "$PKG_DEST_DIR"/mx-tools-"${ARCH_VERSION}"-*.pkg.tar.zst 2>/dev/null || echo 'not found')"
    exit 0
fi

# Clean build directory if requested
if [ "$CLEAN" = true ]; then
    echo "Cleaning build directory and debian artifacts..."
    rm -rf "$BUILD_DIR"
    rm -f debian/*.debhelper.log debian/*.substvars debian/files
    rm -rf debian/.debhelper/ debian/mx-tools/ obj-*/
    rm -f translations/*.qm
fi

# Switching compilers makes CMake discard its cache, and with it CMAKE_BUILD_TYPE, so
# start the build directory's configuration afresh instead.
if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
    CACHED_COMPILER=$(sed -n 's/^CMAKE_CXX_COMPILER:[A-Z]*=//p' "$BUILD_DIR/CMakeCache.txt")
    CACHED_CLANG=false
    if [[ "$CACHED_COMPILER" == *clang* ]]; then
        CACHED_CLANG=true
    fi
    if [ "$CACHED_CLANG" != "$USE_CLANG" ]; then
        echo "Compiler changed; reconfiguring $BUILD_DIR from scratch"
        rm -rf "$BUILD_DIR/CMakeCache.txt" "$BUILD_DIR/CMakeFiles"
    fi
fi

# Create build directory
mkdir -p "$BUILD_DIR"

# Configure CMake with Ninja
echo "Configuring CMake with Ninja generator..."
CMAKE_ARGS=(
    -G Ninja
    -B "$BUILD_DIR"
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
)

# The compiler has to be chosen before CMake's project() detects it, so pass it here
# rather than switching inside CMakeLists.txt.
if [ "$USE_CLANG" = true ]; then
    CMAKE_ARGS+=(-DCMAKE_CXX_COMPILER=clang++)
    echo "Using clang compiler"
fi

# Color compiler diagnostics only when a person is watching; logs stay plain. Pass the
# setting either way, since the cache would otherwise keep the last run's choice.
if [ -t 1 ]; then
    CMAKE_ARGS+=(-DCMAKE_COLOR_DIAGNOSTICS=ON)
else
    CMAKE_ARGS+=(-DCMAKE_COLOR_DIAGNOSTICS=OFF)
fi

cmake "${CMAKE_ARGS[@]}"

# Build the project
echo "Building project with Ninja..."
cmake --build "$BUILD_DIR" --parallel

echo "Build completed successfully!"
echo "Executable: $BUILD_DIR/mx-tools"
