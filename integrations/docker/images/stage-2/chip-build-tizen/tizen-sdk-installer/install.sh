#!/usr/bin/env bash

#
#    Copyright (c) 2021 Project CHIP Authors
#
#    Licensed under the Apache License, Version 2.0 (the "License");
#    you may not use this file except in compliance with the License.
#    You may obtain a copy of the License at
#
#        http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS,
#    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#    See the License for the specific language governing permissions and
#    limitations under the License.
#

set -e

# Default settings options
TIZEN_SDK_ROOT=/opt/tizen-sdk
TIZEN_SDK_DATA_PATH=$HOME/tizen-sdk-data
TIZEN_VERSION=8.0
SECRET_TOOL=false

SCRIPT_NAME=$(basename -- "$(readlink -f "${BASH_SOURCE:?}")")
SCRIPT_DIR=$(dirname -- "$(readlink -f "${BASH_SOURCE:?}")")
DEPENDENCIES=('7zip' 'cpio' 'openjdk-8-jre-headless' 'wget' 'zip')

# If color is available use colors
if which tput >/dev/null 2>&1 && [[ $(tput -T $TERM colors) -ge 8 ]]; then
    COLOR_NONE="$(tput sgr0)"
    COLOR_RED="$(tput setaf 1)"
    COLOR_GREEN="$(tput setaf 2)"
    COLOR_YELLOW="$(tput setaf 3)"
    COLOR_BLUE="$(tput setaf 4)"
fi

# ------------------------------------------------------------------------------
# Help display function
function show_help() {
    echo "Usage: $SCRIPT_NAME [ options .. ]"
    echo "Example: $SCRIPT_NAME --tizen-sdk-path ~/tizen-sdk --tizen-version 7.0 --install-dependencies"
    echo
    echo "Options:"
    echo "  -h, --help                 Display this information"
    echo "  --cpu                      Comma separated list of CPU architectures. Like arm or arm64"
    echo "  --tizen-sdk-path           Set directory for Tizen SDK installation. Default is $TIZEN_SDK_ROOT"
    echo "  --tizen-sdk-data-path      Set directory for Tizen SDK runtime data. Default is $TIZEN_SDK_DATA_PATH"
    echo "  --install-dependencies     This option installs all required dependencies"
    echo "  --tizen-version            Select Tizen version. Default is $TIZEN_VERSION"
    echo "  --override-secret-tool     Circumvent the requirement of having functional D-Bus Secrets service"
    echo
    echo "Note:"
    echo "This script does not install full Tizen SDK. It installs only the necessary"
    echo "parts for Matter SDK to be able to build Tizen applications."
    echo "The option '--install-dependencies' should be able to install all required"
    echo "dependencies on any Debian-based distribution. For other distributions the"
    echo "dependencies should be installed manually."
}

# ------------------------------------------------------------------------------
# Error print function
function error() {
    echo "$COLOR_RED[ERROR]: $1$COLOR_NONE"
}

# ------------------------------------------------------------------------------
# Info print function
function info() {
    echo "$COLOR_GREEN[INFO]: $1$COLOR_NONE"
}

# ------------------------------------------------------------------------------
# Warning print function
function warning() {
    echo "$COLOR_YELLOW[WARNING]: $1$COLOR_NONE"
}

# ------------------------------------------------------------------------------
# Show dependencies
function show_dependencies() {
    warning "Required dependencies for Tizen SDK installation: 7z cpio unzip wget"
    warning "Required dependencies for Tizen SDK: JAVA JRE >=8.0"
}

# ------------------------------------------------------------------------------
# Helper function for downloading packages.
# Usage: download "url_dir_package" ${package_array[@]}
function download() {
    echo "$COLOR_BLUE"

    PKGS=()
    for PKG in "${@:2}"; do
        PKGS+=("-A" "$PKG")
    done

    # Skip downloading if no packages are specified
    [[ ${#PKGS[@]} -eq 0 ]] && return

    wget -r -nd --no-parent -e robots=off --progress=dot:mega "${PKGS[@]}" "$1"

    # Check if the files have been downloaded
    for PKG in "${@:2}"; do
        if [[ ! $(find . -type f -name "$PKG") ]]; then
            error "PKG is missing: $PKG"
            return 1
        fi
    done

    echo -n "$COLOR_NONE"
}

# ------------------------------------------------------------------------------
# Function for installing all dependencies.
function install_dependencies() {
    if ! command -v apt-get &>/dev/null; then
        show_dependencies
        error "Cannot install dependencies - apt package manager not available. Install dependencies manually."
        return 1
    fi

    info "Installing dependencies"
    apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install -fy --no-install-recommends "${DEPENDENCIES[@]}" || return
}

# ------------------------------------------------------------------------------
# Function for unpacking RPM packages.
function unrpm() {
    for PKG in "${@}"; do
        echo "Extracting $PKG..."
        7z x -so "$PKG" | cpio -idmv
    done
}

# ------------------------------------------------------------------------------
# Function for cleaning up temporary files on exit.
function cleanup() {
    rm -rf "${TMP_DIR:?}"
}

# ------------------------------------------------------------------------------
# Function for installing common packages for Tizen SDK.
function install_tizen_sdk_common() {
    mkdir -p "$TIZEN_SDK_ROOT" || return

    info "Tizen SDK installation directory: $TIZEN_SDK_ROOT"

    cd "$TMP_DIR" || return

    # Get Tizen Studio CLI
    info "Downloading Tizen Studio CLI..."

    # Download
    URL="http://download.tizen.org/sdk/tizenstudio/official/binary/"
    PKG_ARR=(
        'certificate-encryptor_1.0.10_ubuntu-64.zip'
        'certificate-generator_0.1.4_ubuntu-64.zip'
        'new-common-cli_2.5.64_ubuntu-64.zip'
        'new-native-cli_2.5.64_ubuntu-64.zip'
        'sdb_4.2.23_ubuntu-64.zip'
        "tizen-$TIZEN_VERSION-core-add-ons_*_ubuntu-64.zip")
    download "$URL" "${PKG_ARR[@]}"

    # Tizen Developer Platform Certificate
    URL="http://download.tizen.org/sdk/extensions/Tizen_IoT_Headless/binary/"
    # Tizen site does not have this package available in version 8.0.
    # Certificates are the same for 7.0 and 8.0, though.
    PKG_ARR=(
        "7.0-iot-things-add-ons_*_ubuntu-64.zip")
    download "$URL" "${PKG_ARR[@]}"

    info "Installing Tizen Studio CLI..."

    unzip -o '*.zip'
    cp -rf data/* "$TIZEN_SDK_ROOT"

    # Cleanup
    rm -rf -- *

}

# Function for installing Tizen SDK (arm).
function install_tizen_sdk_arm() {

    SYSROOT="$TIZEN_SDK_ROOT/platforms/tizen-$TIZEN_VERSION/tizen/rootstraps/tizen-$TIZEN_VERSION-device.core"

    # Get toolchain
    info "Downloading Tizen ARM toolchain..."

    # Download
    URL="http://download.tizen.org/sdk/tizenstudio/official/binary/"
    PKG_ARR=(
        "cross-arm-gcc-9.2_0.1.9_ubuntu-64.zip"
        "sbi-toolchain-gcc-9.2.cpp.app_2.2.16_ubuntu-64.zip")
    download "$URL" "${PKG_ARR[@]}"

    # Get Tizen sysroot
    info "Downloading Tizen ARM sysroot..."

    # Base sysroot
    # Different versions of Tizen have different rootstrap versions
    URL="http://download.tizen.org/sdk/tizenstudio/official/binary/"
    PKG_ARR=(
        "tizen-$TIZEN_VERSION-rs-device.core_*_ubuntu-64.zip")
    download "$URL" "${PKG_ARR[@]}"

    # Base packages
    URL="http://download.tizen.org/releases/milestone/TIZEN/Tizen-$TIZEN_VERSION/Tizen-$TIZEN_VERSION-Base/latest/repos/standard/packages/armv7l/"
    PKG_ARR=(
        'iniparser-*.armv7l.rpm'
        'libblkid-devel-*.armv7l.rpm'
        'libcap-*.armv7l.rpm'
        'libffi-devel-*.armv7l.rpm'
        'liblzma-*.armv7l.rpm'
        'libmount-devel-*.armv7l.rpm'
        'libncurses6-*.armv7l.rpm'
        'libreadline-*.armv7l.rpm'
        'libuuid-*.armv7l.rpm'
        'pcre-devel-*.armv7l.rpm'
        'readline-devel-*.armv7l.rpm'
        'xdgmime-*.armv7l.rpm')
    download "$URL" "${PKG_ARR[@]}"

    # Unified packages
    URL="http://download.tizen.org/releases/milestone/TIZEN/Tizen-$TIZEN_VERSION/Tizen-$TIZEN_VERSION-Unified/latest/repos/standard/packages/armv7l/"
    PKG_ARR=(
        'app-core-common-*.armv7l.rpm'
        'aul-0*.armv7l.rpm'
        'aul-devel-*.armv7l.rpm'
        'bluetooth-frwk-0*.armv7l.rpm'
        'bundle-0*.armv7l.rpm'
        'bundle-devel-*.armv7l.rpm'
        'buxton2-*.armv7l.rpm'
        'capi-network-bluetooth-0*.armv7l.rpm'
        'capi-network-bluetooth-devel-*.armv7l.rpm'
        'capi-network-nsd-*.armv7l.rpm'
        'capi-network-thread-*.armv7l.rpm'
        'capi-system-peripheral-io-*.armv7l.rpm'
        'capi-system-peripheral-io-devel-*.armv7l.rpm'
        'capi-system-resource-1*.armv7l.rpm'
        'cynara-devel-*.armv7l.rpm'
        'dbus-1*.armv7l.rpm'
        'dbus-devel-*.armv7l.rpm'
        'dbus-libs-1*.armv7l.rpm'
        'glib2-devel-2*.armv7l.rpm'
        'hal-api-common-*.armv7l.rpm'
        'hal-api-sensor-*.armv7l.rpm'
        'json-glib-devel-*.armv7l.rpm'
        'libcynara-client-*.armv7l.rpm'
        'libcynara-commons-*.armv7l.rpm'
        'libdns_sd-*.armv7l.rpm'
        'libjson-glib-*.armv7l.rpm'
        'libnsd-dns-sd-*.armv7l.rpm'
        'libsessiond-0*.armv7l.rpm'
        'libsystemd-*.armv7l.rpm'
        'libtzplatform-config-*.armv7l.rpm'
        'parcel-0*.armv7l.rpm'
        'parcel-devel-*.armv7l.rpm'
        'pkgmgr-info-*.armv7l.rpm'
        'sensord-*.armv7l.rpm'
        'sensord-devel-*.armv7l.rpm'
        'sensord-dummy-*.armv7l.rpm'
        'vconf-compat-*.armv7l.rpm'
        'vconf-internal-keys-devel-*.armv7l.rpm')
    download "$URL" "${PKG_ARR[@]}"

    # Unified packages (snapshots)
    URL="http://download.tizen.org/snapshots/TIZEN/Tizen/Tizen-Unified/latest/repos/standard/packages/armv7l/"
    PKG_ARR=()
    download "$URL" "${PKG_ARR[@]}"

    info "Installing Tizen ARM SDK..."

    unzip -o '*.zip'
    cp -rf data/* "$TIZEN_SDK_ROOT"

    unrpm *.rpm
    cp -rf lib usr "$SYSROOT"

    # Make symbolic links relative
    find "$SYSROOT/usr/lib" -maxdepth 1 -type l | while IFS= read -r LNK; do
        ln -sf "$(basename "$(readlink "$LNK")")" "$LNK"
    done

    ln -sf ../../lib/libcap.so.2 "$SYSROOT/usr/lib/libcap.so"
    ln -sf openssl3.pc "$SYSROOT/usr/lib/pkgconfig/openssl.pc"

    # Cleanup
    rm -rf -- *

}

# Function for installing Tizen SDK (arm64).
function install_tizen_sdk_arm64() {

    SYSROOT="$TIZEN_SDK_ROOT/platforms/tizen-$TIZEN_VERSION/tizen/rootstraps/tizen-$TIZEN_VERSION-device64.core"

    # Get toolchain
    info "Downloading Tizen ARM64 toolchain..."

    # Download
    URL="http://download.tizen.org/sdk/tizenstudio/official/binary/"
    PKG_ARR=(
        "cross-aarch64-gcc-9.2_0.1.9_ubuntu-64.zip"
        "sbi-toolchain-gcc-9.2.cpp.app_2.2.16_ubuntu-64.zip")
    download "$URL" "${PKG_ARR[@]}"

    # Get Tizen sysroot
    info "Downloading Tizen ARM64 sysroot..."

    # Base sysroot
    # Different versions of Tizen have different rootstrap versions
    URL="http://download.tizen.org/sdk/tizenstudio/official/binary/"
    PKG_ARR=(
        "tizen-$TIZEN_VERSION-rs-device64.core_*_ubuntu-64.zip")
    download "$URL" "${PKG_ARR[@]}"

    # Base packages
    URL="http://download.tizen.org/releases/milestone/TIZEN/Tizen-$TIZEN_VERSION/Tizen-$TIZEN_VERSION-Base/latest/repos/standard/packages/aarch64/"
    PKG_ARR=(
        'iniparser-*.aarch64.rpm'
        'libblkid-devel-*.aarch64.rpm'
        'libcap-*.aarch64.rpm'
        'libffi-devel-*.aarch64.rpm'
        'liblzma-*.aarch64.rpm'
        'libmount-devel-*.aarch64.rpm'
        'libncurses6-*.aarch64.rpm'
        'libreadline-*.aarch64.rpm'
        'libuuid-*.aarch64.rpm'
        'pcre-devel-*.aarch64.rpm'
        'readline-devel-*.aarch64.rpm'
        'xdgmime-*.aarch64.rpm')
    download "$URL" "${PKG_ARR[@]}"

    # Unified packages
    URL="http://download.tizen.org/releases/milestone/TIZEN/Tizen-$TIZEN_VERSION/Tizen-$TIZEN_VERSION-Unified/latest/repos/standard/packages/aarch64/"
    PKG_ARR=(
        'app-core-common-*.aarch64.rpm'
        'aul-0*.aarch64.rpm'
        'aul-devel-*.aarch64.rpm'
        'bluetooth-frwk-0*.aarch64.rpm'
        'bundle-0*.aarch64.rpm'
        'bundle-devel-*.aarch64.rpm'
        'buxton2-*.aarch64.rpm'
        'capi-network-bluetooth-0*.aarch64.rpm'
        'capi-network-bluetooth-devel-*.aarch64.rpm'
        'capi-network-nsd-*.aarch64.rpm'
        'capi-network-thread-*.aarch64.rpm'
        'capi-system-peripheral-io-*.aarch64.rpm'
        'capi-system-peripheral-io-devel-*.aarch64.rpm'
        'capi-system-resource-1*.aarch64.rpm'
        'cynara-devel-*.aarch64.rpm'
        'dbus-1*.aarch64.rpm'
        'dbus-devel-*.aarch64.rpm'
        'dbus-libs-1*.aarch64.rpm'
        'glib2-devel-2*.aarch64.rpm'
        'hal-api-common-*.aarch64.rpm'
        'hal-api-sensor-*.aarch64.rpm'
        'json-glib-devel-*.aarch64.rpm'
        'libcynara-client-*.aarch64.rpm'
        'libcynara-commons-*.aarch64.rpm'
        'libdns_sd-*.aarch64.rpm'
        'libjson-glib-*.aarch64.rpm'
        'libnsd-dns-sd-*.aarch64.rpm'
        'libsessiond-0*.aarch64.rpm'
        'libsystemd-*.aarch64.rpm'
        'libtzplatform-config-*.aarch64.rpm'
        'parcel-0*.aarch64.rpm'
        'parcel-devel-*.aarch64.rpm'
        'pkgmgr-info-*.aarch64.rpm'
        'sensord-*.aarch64.rpm'
        'sensord-devel-*.aarch64.rpm'
        'sensord-dummy-*.aarch64.rpm'
        'vconf-compat-*.aarch64.rpm'
        'vconf-internal-keys-devel-*.aarch64.rpm'
    )
    download "$URL" "${PKG_ARR[@]}"

    # Unified packages (snapshots)
    URL="http://download.tizen.org/snapshots/TIZEN/Tizen/Tizen-Unified/latest/repos/standard/packages/aarch64/"
    PKG_ARR=()
    download "$URL" "${PKG_ARR[@]}"

    info "Installing Tizen ARM64 SDK..."

    unzip -o '*.zip'
    cp -rf data/* "$TIZEN_SDK_ROOT"

    info "Installing Tizen ARM64 sysroot..."

    unrpm *.rpm
    cp -rf lib64 usr "$SYSROOT"

    # Make symbolic links relative
    find "$SYSROOT/usr/lib64" -maxdepth 1 -type l | while IFS= read -r LNK; do
        ln -sf "$(basename "$(readlink "$LNK")")" "$LNK"
    done

    ln -sf ../../lib64/libcap.so.2 "$SYSROOT/usr/lib64/libcap.so"
    ln -sf openssl3.pc "$SYSROOT/usr/lib64/pkgconfig/openssl.pc"

    # Cleanup
    rm -rf -- *

}

function install_tizen_sdk_finalize() {

    # Install secret tool or not
    if ("$SECRET_TOOL"); then
        info "Overriding secret tool..."
        install "$SCRIPT_DIR/secret-tool.py" "$TIZEN_SDK_ROOT/tools/certificate-encryptor/secret-tool"
    fi

    # Configure Tizen CLI
    echo "TIZEN_SDK_INSTALLED_PATH=$TIZEN_SDK_ROOT" >"$TIZEN_SDK_ROOT/sdk.info"
    echo "TIZEN_SDK_DATA_PATH=$TIZEN_SDK_DATA_PATH" >>"$TIZEN_SDK_ROOT/sdk.info"
    ln -sf "$TIZEN_SDK_DATA_PATH/.tizen-cli-config" "$TIZEN_SDK_ROOT/tools/.tizen-cli-config"

    # Use Tizen developer platform certificate as defaults
    cp "$TIZEN_SDK_ROOT"/tools/certificate-generator/certificates/distributor/sdk-platform/* \
        "$TIZEN_SDK_ROOT"/tools/certificate-generator/certificates/distributor/

    info "Done."
    echo

    warning "Before proceeding with Matter export environment variables as follows:"
    echo -n "$COLOR_YELLOW"
    echo "export TIZEN_VERSION=\"$TIZEN_VERSION\""
    echo "export TIZEN_SDK_ROOT=\"$(realpath "$TIZEN_SDK_ROOT")\""

    if [ "$INSTALL_ARM" = true ]; then
        echo "export TIZEN_SDK_TOOLCHAIN=\"\$TIZEN_SDK_ROOT/tools/arm-linux-gnueabi-gcc-9.2\""
        echo "export TIZEN_SDK_SYSROOT=\"\$TIZEN_SDK_ROOT/platforms/tizen-$TIZEN_VERSION/tizen/rootstraps/tizen-$TIZEN_VERSION-device.core\""
        echo "export PATH=\"\$TIZEN_SDK_TOOLCHAIN/bin:\$TIZEN_SDK_ROOT/tools/ide/bin:\$TIZEN_SDK_ROOT/tools:\$PATH\""
    fi

    if [ "$INSTALL_ARM64" = true ]; then
        echo "export TIZEN_SDK_TOOLCHAIN_ARM64=\"\$TIZEN_SDK_ROOT/tools/aarch64-linux-gnu-gcc-9.2\""
        echo "export TIZEN_SDK_SYSROOT_ARM64=\"\$TIZEN_SDK_ROOT/platforms/tizen-$TIZEN_VERSION/tizen/rootstraps/tizen-$TIZEN_VERSION-device64.core\""
        echo "export PATH=\"\$TIZEN_SDK_TOOLCHAIN_ARM64/bin:\$TIZEN_SDK_ROOT/tools/ide/bin:\$TIZEN_SDK_ROOT/tools:\$PATH\""
    fi

    echo -n "$COLOR_NONE"
}

while (($#)); do
    case $1 in
        -h | --help)
            show_help
            exit 0
            ;;
        --cpu)
            IFS=',' read -r -a array <<<"$2"
            for CPU in "${array[@]}"; do
                if [ "$CPU" == "arm" ]; then
                    INSTALL_ARM=true
                elif [ "$CPU" == "arm64" ]; then
                    INSTALL_ARM64=true
                else
                    error "Invalid CPU: $CPU. Use --help"
                    exit 1
                fi
            done
            shift
            ;;
        --tizen-sdk-path)
            TIZEN_SDK_ROOT="$2"
            shift
            ;;
        --tizen-sdk-data-path)
            TIZEN_SDK_DATA_PATH="$2"
            shift
            ;;
        --tizen-version)
            TIZEN_VERSION=$2
            shift
            ;;
        --install-dependencies)
            INSTALL_DEPENDENCIES=true
            ;;
        --override-secret-tool)
            SECRET_TOOL=true
            ;;
        *)
            error "Wrong options usage!"
            exit 1
            ;;
    esac
    shift
done

# ------------------------------------------------------------------------------
# Verify passed arguments and flags
if [ -z "$INSTALL_ARM" ] && [ -z "$INSTALL_ARM64" ]; then
    error "No CPU architecture provided. Use --help."
    exit 1
fi

# ------------------------------------------------------------------------------
# Prepare a temporary directory and cleanup
trap cleanup EXIT
TMP_DIR=$(mktemp -d)
info "Created tmp directory $TMP_DIR"

# ------------------------------------------------------------------------------
# Checks if the user need install dependencies
if [ "$INSTALL_DEPENDENCIES" = true ]; then
    if ! install_dependencies; then
        error "Cannot install dependencies, please use this script as sudo user or root."
        show_dependencies
        exit 1
    fi
fi

# ------------------------------------------------------------------------------
# Checking dependencies needed to install Tizen platform
info "Checking required tools: 7z, cpio, java, unzip, wget"
for PKG in '7z' 'cpio' 'java' 'unzip' 'wget'; do
    if ! command -v "$PKG" &>/dev/null; then
        error "Required tool not found: $PKG"
        dep_lost=1
    fi
done
if [[ $dep_lost ]]; then
    echo "[HINT]: sudo apt-get install ${DEPENDENCIES[*]}"
    exit 1
fi

# ------------------------------------------------------------------------------
# Installation Tizen SDK
install_tizen_sdk_common

if [ "$INSTALL_ARM" = true ]; then
    install_tizen_sdk_arm
fi

if [ "$INSTALL_ARM64" = true ]; then
    install_tizen_sdk_arm64
fi

install_tizen_sdk_finalize
