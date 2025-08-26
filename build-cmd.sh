#!/usr/bin/env bash

cd "$(dirname "$0")"

# turn on verbose debugging output for parabuild logs.
exec 4>&1; export BASH_XTRACEFD=4; set -x

# make errors fatal
set -e

# complain about unset env variables
set -u

if [ -z "$AUTOBUILD" ] ; then
    exit 1
fi

if [ "$OSTYPE" = "cygwin" ] ; then
    autobuild="$(cygpath -u $AUTOBUILD)"
else
    autobuild="$AUTOBUILD"
fi

top="$(pwd)"
stage="$(pwd)/stage"

# Extract version from vcpkg.json
ENTROPYCORE_VERSION=$(grep -o '"version": "[^"]*"' vcpkg.json | cut -d'"' -f4)

# load autobuild provided shell functions and variables
source_environment_tempfile="$stage/source_environment.sh"
"$autobuild" source_environment > "$source_environment_tempfile"
. "$source_environment_tempfile"

build=${AUTOBUILD_BUILD_ID:=0}

# prepare the staging dirs
mkdir -p "$stage/LICENSES"
mkdir -p "$stage/include/EntropyCore"
mkdir -p "$stage/lib/release"

# Bootstrap vcpkg if not already present
VCPKG_ROOT="${VCPKG_ROOT:-$top/vcpkg}"
if [ ! -d "$VCPKG_ROOT" ]; then
    echo "Bootstrapping vcpkg..."
    git clone https://github.com/Microsoft/vcpkg.git "$VCPKG_ROOT"
    "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
fi

# Set up build directory
BUILD_DIR="$top/build_autobuild"
mkdir -p "$BUILD_DIR"

case "$AUTOBUILD_PLATFORM" in
    darwin*)
        # Configure with CMake using vcpkg toolchain
        cmake -S "$top" -B "$BUILD_DIR" \
            -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
            -DVCPKG_TARGET_TRIPLET=x64-osx \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_SHARED_LIBS=OFF \
            -DENTROPY_BUILD_TESTS=OFF \
            -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" \
            -DCMAKE_OSX_DEPLOYMENT_TARGET=13.3 \
            -DCMAKE_INSTALL_PREFIX="$stage"
        
        # Build the project
        cmake --build "$BUILD_DIR" --config Release
        
        # Install to staging directory
        cmake --install "$BUILD_DIR" --config Release
        
        # Move installed files to expected locations for autobuild
        mkdir -p "$stage/lib/release"
        if [ -f "$stage/lib/libEntropyCore.a" ]; then
            mv "$stage/lib/libEntropyCore.a" "$stage/lib/release/"
        fi
        
        # Move headers to EntropyCore subdirectory
        if [ -d "$stage/include" ]; then
            mkdir -p "$stage/include/EntropyCore"
            # Move all headers preserving structure
            for dir in Core Concurrency Debug Graph Logging TypeSystem; do
                if [ -d "$stage/include/$dir" ]; then
                    mv "$stage/include/$dir" "$stage/include/EntropyCore/"
                fi
            done
            # Move root headers
            for header in EntropyCore.h CoreCommon.h ServiceLocator.h; do
                if [ -f "$stage/include/$header" ]; then
                    mv "$stage/include/$header" "$stage/include/EntropyCore/"
                fi
            done
        fi
        
        # Code signing for macOS if configured
        if [[ -z "${build_secrets_checkout:-}" ]]; then
            echo '$build_secrets_checkout not set; skipping codesign' >&2
        else
            CONFIG_FILE="$build_secrets_checkout/code-signing-osx/config.sh"
            if [[ ! -f "$CONFIG_FILE" ]]; then
                echo "No config file found; skipping codesign."
            else
                source $CONFIG_FILE
                pushd "$stage/lib/release"
                for lib in *.a; do
                    if [ -f "$lib" ]; then
                        codesign --force --timestamp --sign "$APPLE_SIGNATURE" "$lib"
                    fi
                done
                popd
            fi
        fi
    ;;
    linux*)
        # Configure with CMake using vcpkg toolchain
        cmake -S "$top" -B "$BUILD_DIR" \
            -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
            -DVCPKG_TARGET_TRIPLET=x64-linux \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_SHARED_LIBS=OFF \
            -DENTROPY_BUILD_TESTS=OFF \
            -DCMAKE_INSTALL_PREFIX="$stage"
        
        # Build the project
        cmake --build "$BUILD_DIR" --config Release
        
        # Install to staging directory
        cmake --install "$BUILD_DIR" --config Release
        
        # Move installed files to expected locations for autobuild
        mkdir -p "$stage/lib/release"
        if [ -f "$stage/lib/libEntropyCore.a" ]; then
            mv "$stage/lib/libEntropyCore.a" "$stage/lib/release/"
        fi
        
        # Move headers to EntropyCore subdirectory
        if [ -d "$stage/include" ]; then
            mkdir -p "$stage/include/EntropyCore"
            # Move all headers preserving structure
            for dir in Core Concurrency Debug Graph Logging TypeSystem; do
                if [ -d "$stage/include/$dir" ]; then
                    mv "$stage/include/$dir" "$stage/include/EntropyCore/"
                fi
            done
            # Move root headers
            for header in EntropyCore.h CoreCommon.h ServiceLocator.h; do
                if [ -f "$stage/include/$header" ]; then
                    mv "$stage/include/$header" "$stage/include/EntropyCore/"
                fi
            done
        fi
    ;;
esac

# Clean up any empty cmake directories that might have been created
if [ -d "$stage/lib/cmake" ]; then
    rm -rf "$stage/lib/cmake"
fi

# Create VERSION.txt
echo "$ENTROPYCORE_VERSION.$build" > "$stage/VERSION.txt"

# Copy license files - both to LICENSES directory and root for autobuild package
cp "$top/LICENSE.md" "$stage/LICENSES/EntropyCore.txt"
cp "$top/LICENSE.md" "$stage/LICENSE.md"