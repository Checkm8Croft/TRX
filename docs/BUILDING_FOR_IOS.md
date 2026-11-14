## Building for iOS

This guide describes the iOS build workflow using Meson.

The build is a meson cross build (`arm64_device_cross_file.txt`). Everything it
links statically is vendored in `deps/`, so the only things it needs from the
host are a compiler and Apple's iPhoneOS SDK.

## Getting Started

### macOS

Install Xcode and the iOS SDK

### Linux

Install these dependencies:

```zsh
clang lld ninja-build meson pkgconf python3 binutils
```

Install an iOS SDK with Theos:

```zsh
git clone --depth 1 https://github.com/theos/sdks $THEOS/sdks
export IPHONEOS_SDK=$THEOS/iPhoneOS16.5.sdk
```

## Building

The dependencies already built for iOS are located in `tools/shared/ios/deps`

Configure the build: 

```zsh
meson setup build-ios-arm64 src \
    --cross-file tools/shared/ios/arm64_device_cross_file.txt --buildtype release
```

Compile TRX: `ninja -C build-ios-arm64`

Generate the .ipa: `python3 tools/shared/ios/gen_ipa.py`

## Installing TRX

For installing TRX on your iDevice, you'll need to sideload the .ipa file 

## Running TRX

For running TRX, you'll have to open the game on your iDevice first, and you'll see the error of `No Playable Mods Avaible`, this will generate the TRX folder in the Files app. 
Now you can open the TRX folder and inside there you can copy the game files.