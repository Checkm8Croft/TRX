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
export IPHONEOS_SDK=$THEOS/sdks/iPhoneOS16.5.sdk
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

### iOS 27

From iOS 27, UIKit refuses to launch an app that was linked against the iOS 27
SDK (or later) and doesn't use the UIScene life cycle, which SDL2 doesn't. Such
a build crashes at launch on iOS 27, before `main()` runs, so there is no log
and no TRX folder. The check goes by the SDK version recorded in the binary, so
`tools/shared/ios/clang-ios-arm64` records iOS 26 when linking
(`-Wl,-platform_version,ios,16.0.0,26.0`). On macOS, `vtool -show-build TRX`
shows what a binary says. This is a workaround; the real fix is an SDL with
UIScene support.

## Installing TRX

For installing TRX on your iDevice, you'll need to sideload the .ipa file 

## Running TRX

For running TRX, you'll have to open the game on your iDevice first, and you'll see the error of `No Playable Mods Avaible`, this will generate the TRX folder in the Files app. 
Now you can open the TRX folder and inside there you can copy the game files.