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
    # Captured before debuild runs: debian/rules appends a distro release
    # suffix (e.g. "mx25" on Trixie) to debian/changelog mid-build, but
    # dpkg-buildpackage itself fixes the genbuildinfo/genchanges output
    # filenames at invocation start, before that mutation happens. Reading
    # the version after debuild would pick up the mutated changelog and
    # compute an artifact prefix that doesn't match what was actually produced.
    DEBIAN_VERSION=$(dpkg-parsechangelog -SVersion)
    DEBIAN_ARTIFACT_PREFIX="${DEBIAN_SOURCE}_${DEBIAN_VERSION#*:}_${DEBIAN_ARCH}"

    # debian/rules may append a distro release suffix (e.g. "mx25" on Trixie)
    # to debian/changelog while building. Only restore the changelog
    # afterwards if it was pristine beforehand, so any pre-existing
    # uncommitted edit to it is never discarded.
    CHANGELOG_WAS_CLEAN=true
    git diff --quiet -- debian/changelog || CHANGELOG_WAS_CLEAN=false

    # Binary-only build: debian/rules appends a distro suffix (e.g. "mx25")
    # to debian/changelog mid-build for OBS multi-distro packaging. That
    # only stays consistent for a binary-only build - a full source+binary
    # build runs dpkg-source before the suffix is added and dpkg-genbuildinfo
    # after, so the two disagree on the version and the build fails looking
    # for a .dsc that was never produced under the suffixed name.
    echo "Building Debian package..."
    debuild -us -uc -b

    # The current build's manifest lists its binary and source artifacts,
    # including buildinfo. Never sweep unrelated files from the parent directory.
    DEBIAN_CHANGES="../${DEBIAN_ARTIFACT_PREFIX}.changes"
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
        if [[ "$artifact" == */* || "$artifact" == "." || "$artifact" == ".." || ! -f "../$artifact" ]]; then
            echo "Error: invalid or missing build artifact: $artifact"
            exit 1
        fi
    done

    echo "Creating debs directory and moving debian artifacts..."
    mkdir -p debs
    for artifact in "${DEBIAN_ARTIFACTS[@]}"; do
        mv -- "../$artifact" debs/
    done
    mv -- "$DEBIAN_CHANGES" debs/
    if [ -f "../${DEBIAN_ARTIFACT_PREFIX}.build" ]; then
        mv -- "../${DEBIAN_ARTIFACT_PREFIX}.build" debs/
    fi

    echo "Cleaning build directory and debian artifacts..."
    rm -rf "$BUILD_DIR"
    rm -f debian/*.debhelper.log debian/*.substvars debian/files
    rm -rf debian/.debhelper/ debian/mx-tools/ obj-*/
    rm -f translations/*.qm
    if [ "$CHANGELOG_WAS_CLEAN" = true ]; then
        git checkout -- debian/changelog
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

    ARCH_BUILDDIR=$(mktemp -d -p "$PWD" archpkgbuild.XXXXXX)
    trap 'rm -rf "$ARCH_BUILDDIR"' EXIT

    # Clean previous build artifacts
    rm -rf pkg *.pkg.tar.zst

    PKG_DEST_DIR="$PWD/build"
    mkdir -p "$PKG_DEST_DIR"

    # Build package (without --clean to preserve directories)
    BUILDDIR="$ARCH_BUILDDIR" PKGDEST="$PKG_DEST_DIR" PKGVER="$ARCH_VERSION" makepkg -f

    # Clean makepkg artifacts
    echo "Cleaning makepkg artifacts..."
    rm -rf pkg

    echo "Arch Linux package build completed!"
    echo "Package: $(ls build/*.pkg.tar.zst 2>/dev/null || echo 'not found')"
    echo "Binary available at: build/mx-tools"
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

if [ "$USE_CLANG" = true ]; then
    CMAKE_ARGS+=(-DUSE_CLANG=ON)
    echo "Using clang compiler"
fi

cmake "${CMAKE_ARGS[@]}"

# Build the project
echo "Building project with Ninja..."
cmake --build "$BUILD_DIR" --parallel

echo "Build completed successfully!"
echo "Executable: $BUILD_DIR/mx-tools"
