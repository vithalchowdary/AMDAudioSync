name: Build AMDAudioSync

on:
  push:
    branches: [ main, master ]
  pull_request:
    branches: [ main, master ]
  workflow_dispatch:

jobs:
  build:
    runs-on: macos-latest
    steps:
    - name: Checkout Repository
      uses: actions/checkout@v4

    - name: Clone MacKernelSDK
      run: git clone --depth 1 https://github.com/acidanthera/MacKernelSDK.git

    - name: Download Lilu DEBUG
      env:
        GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
      run: |
        LILU_URL=$(curl -sH "Authorization: token $GITHUB_TOKEN" https://api.github.com/repos/acidanthera/Lilu/releases/latest | grep browser_download_url | grep DEBUG.zip | cut -d '"' -f 4)
        curl -L "$LILU_URL" -o Lilu.zip
        unzip -q Lilu.zip "Lilu.kext/*"
        
        # Physical verification of the headers
        if [ ! -f "Lilu.kext/Contents/Resources/Headers/kern_patcher.hpp" ]; then
          echo "Lilu headers failed to extract!"
          exit 1
        fi

    - name: Build Kext
      run: |
        xcodebuild -project AMDAudioSync.xcodeproj -configuration Release ARCHS=x86_64 \
        HEADER_SEARCH_PATHS="$(pwd)/Lilu.kext/Contents/Resources $(pwd)/MacKernelSDK/Headers" \
        SYSTEM_HEADER_SEARCH_PATHS="$(pwd)/MacKernelSDK/Headers" \
        OTHER_LDFLAGS="-Wl,-unexported_symbol,__ZN19KernelPatcherPlugin* $(pwd)/Lilu.kext/Contents/MacOS/Lilu" \
        clean build

    - name: Upload Artifact
      uses: actions/upload-artifact@v4
      with:
        name: AMDAudioSync-Release
        path: build/Release/AMDAudioSync.kext
