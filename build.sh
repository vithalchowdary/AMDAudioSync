#!/bin/bash
set -e

echo "=== Building AMDAudioSync.kext ==="
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Use the same Lilu version as the Lilu.kext in your EFI (kmutil showloaded | grep -i lilu).
LILU_VERSION="${LILU_VERSION:-1.7.3}"

# 1. MacKernelSDK
if [ ! -d "MacKernelSDK" ]; then
    echo "Cloning Acidanthera MacKernelSDK..."
    git clone --depth 1 https://github.com/acidanthera/MacKernelSDK.git
fi

# 2. Lilu SOURCE at the matching tag: provides <Headers/...> and Library/plugin_start.cpp,
#    which defines the kext entry points (<PluginName>_kern_start / _kern_stop).
if [ ! -d "Lilu" ]; then
    echo "Cloning Lilu $LILU_VERSION source..."
    git clone --depth 1 --branch "$LILU_VERSION" https://github.com/acidanthera/Lilu.git
fi
test -f Lilu/Lilu/Library/plugin_start.cpp || { echo "plugin_start.cpp not found in Lilu/Lilu/Library"; ls Lilu Lilu/Lilu; exit 1; }

# 3. Lilu DEBUG kext of the same version, used only for linking
if [ ! -d "Lilu.kext" ]; then
    echo "Downloading Lilu $LILU_VERSION DEBUG kext..."
    AUTH=()
    if [ -n "$GITHUB_TOKEN" ]; then AUTH=(-H "Authorization: token $GITHUB_TOKEN"); fi
    LILU_URL=$(curl -s "${AUTH[@]}" "https://api.github.com/repos/acidanthera/Lilu/releases/tags/$LILU_VERSION" | grep browser_download_url | grep DEBUG.zip | cut -d '"' -f 4)
    if [ -z "$LILU_URL" ]; then
        echo "Failed to fetch Lilu $LILU_VERSION download URL."
        exit 1
    fi
    curl -L "$LILU_URL" -o Lilu.zip
    unzip -q Lilu.zip "Lilu.kext/*"
    rm -f Lilu.zip
fi
echo "Lilu kext version: $(/usr/libexec/PlistBuddy -c 'Print CFBundleVersion' Lilu.kext/Contents/Info.plist)"

# 4. Build
echo "Compiling kext..."
xcodebuild -project AMDAudioSync.xcodeproj -configuration Release ARCHS=x86_64 \
HEADER_SEARCH_PATHS="$(pwd)/Lilu/Lilu $(pwd)/MacKernelSDK/Headers" \
SYSTEM_HEADER_SEARCH_PATHS="$(pwd)/MacKernelSDK/Headers" \
OTHER_CFLAGS="-mkernel -fno-builtin -fno-common -fno-stack-protector" \
OTHER_CPLUSPLUSFLAGS="-mkernel -fapple-kext -fno-builtin -fno-common -fno-stack-protector -fno-exceptions -fno-rtti" \
OTHER_LDFLAGS="-mkernel -nostdlib -Wl,-kext -Wl,-unexported_symbol,__ZN19KernelPatcherPlugin* $(pwd)/Lilu.kext/Contents/MacOS/Lilu -lkmodc++ -lkmod -lcc_kext -undefined dynamic_lookup -Wl,-ld_classic" \
clean build

BIN="build/Release/AMDAudioSync.kext/Contents/MacOS/AMDAudioSync"

# 5. Fail the build if the kext entry points are missing. '-undefined dynamic_lookup' hides this
#    otherwise, and such a kext panics the machine at boot.
echo "--- symbol check ---"
nm -g "$BIN" | grep -E "AMDAudioSync_(kern_start|kern_stop|config)" || true
if ! nm -g "$BIN" | grep -q "AMDAudioSync_kern_start" || ! nm -g "$BIN" | grep -q "AMDAudioSync_kern_stop"; then
    echo "ERROR: AMDAudioSync_kern_start/kern_stop are not defined in the kext" ; exit 1
fi
if nm -u "$BIN" | grep -E "kern_start|kern_stop|PRODUCT_NAME"; then
    echo "ERROR: unresolved entry-point symbols above" ; exit 1
fi
if nm -g "$BIN" | grep -q "PRODUCT_NAME"; then
    echo "ERROR: PRODUCT_NAME macro was not defined (plugin would be named literally PRODUCT_NAME)" ; exit 1
fi

echo "=== Build complete! Output is in build/Release/AMDAudioSync.kext ==="
