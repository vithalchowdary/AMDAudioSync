#!/bin/bash
set -e

echo "=== Building AMDAudioSync.kext ==="
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 1. Clone MacKernelSDK
if [ ! -d "MacKernelSDK" ]; then
    echo "Cloning Acidanthera MacKernelSDK..."
    git clone --depth 1 https://github.com/acidanthera/MacKernelSDK.git
fi

# 2. Download Lilu DEBUG 
if [ ! -d "Lilu.kext" ]; then
    echo "Downloading latest Lilu DEBUG kext for headers & linking..."
    # If GITHUB_TOKEN is available, use it to avoid rate limits, else fallback to standard curl
    if [ -n "$GITHUB_TOKEN" ]; then
        LILU_URL=$(curl -sH "Authorization: token $GITHUB_TOKEN" https://api.github.com/repos/acidanthera/Lilu/releases/latest | grep browser_download_url | grep DEBUG.zip | cut -d '"' -f 4)
    else
        LILU_URL=$(curl -s https://api.github.com/repos/acidanthera/Lilu/releases/latest | grep browser_download_url | grep DEBUG.zip | cut -d '"' -f 4)
    fi
    
    if [ -z "$LILU_URL" ]; then
        echo "Failed to fetch Lilu download URL. GitHub API rate limit likely exceeded."
        exit 1
    fi
    
    curl -L "$LILU_URL" -o Lilu.zip
    unzip -q Lilu.zip "Lilu.kext/*"
    rm -f Lilu.zip
fi

# 3. Build Kext with explicit header paths
echo "Compiling kext..."
xcodebuild -project AMDAudioSync.xcodeproj -configuration Release ARCHS=x86_64 \
HEADER_SEARCH_PATHS="$(pwd)/Lilu.kext/Contents/Resources $(pwd)/MacKernelSDK/Headers" \
SYSTEM_HEADER_SEARCH_PATHS="$(pwd)/MacKernelSDK/Headers" \
OTHER_LDFLAGS="-Wl,-unexported_symbol,__ZN19KernelPatcherPlugin* $(pwd)/Lilu.kext/Contents/MacOS/Lilu" \
clean build

echo "=== Build complete! Output is in build/Release/AMDAudioSync.kext ==="
