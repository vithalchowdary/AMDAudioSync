#!/bin/bash
set -e

echo "=== Building AMDAudioSync.kext ==="
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Clone MacKernelSDK if not present
if [ ! -d "MacKernelSDK" ]; then
    echo "Cloning Acidanthera MacKernelSDK..."
    git clone --depth 1 https://github.com/acidanthera/MacKernelSDK.git
fi

# Fetch debug Lilu.kext if not present
if [ ! -d "Lilu.kext" ]; then
    echo "Downloading latest Lilu DEBUG kext for headers & linking..."
    LILU_URL=$(curl -s https://api.github.com/repos/acidanthera/Lilu/releases/latest | grep browser_download_url | grep DEBUG.zip | cut -d '"' -f 4)
    curl -L "$LILU_URL" -o Lilu.zip
    unzip -q Lilu.zip "Lilu.kext/*"
    rm -f Lilu.zip
fi

xcodebuild -project AMDAudioSync.xcodeproj \
  -configuration Release \
  ARCHS=x86_64 \
  LIBRARY_SEARCH_PATHS="\$(PROJECT_DIR)/MacKernelSDK/Library/x86_64" \
  OTHER_LDFLAGS="-Wl,-kext -Wl,-undefined,dynamic_lookup \$(PROJECT_DIR)/MacKernelSDK/Library/x86_64/libkmod.a" \
  clean build
echo "=== Build complete! Output is in build/Release/AMDAudioSync.kext ==="