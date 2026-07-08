## Building for iOS

This guide describes the iOS build workflow using Meson.

## Building on macOS/Linux

The dependencies are located in `tools/shared/ios/deps`

Configure the build: `meson setup build-ios-arm64 src --cross-file tools/shared/ios/arm64_device_cross_file.txt --buildtype debug`

Compile TRX: `meson compile -C build-ios-arm64`

Generate the .ipa: `./tools/shared/ios/gen_ipa`

## Installing TRX

For installing TRX on your iDevice, you'll need to sideload the .ipa file 

## Running TRX

For running TRX, you'll have to open the game on your iDevice first, and you'll see the error of No Playable Mods Avaible, this will generate the TRX folder in the Files app. So open the TRX folder and there you can copy the game files