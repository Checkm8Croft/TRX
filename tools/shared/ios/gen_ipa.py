#!/usr/bin/env python3
import os
import subprocess
import shutil

def create_app():
    os.chdir("build-ios-arm64")
    os.makedirs("Payload/TRX.app")
    
def copy_files():
    shutil.copy("TRX", "Payload/TRX.app/TRX")
    shutil.copy("../tools/shared/ios/Info.plist", "Payload/TRX.app/Info.plist")
    shutil.copy("../tools/shared/ios/LaunchScreen.storyboardc", "Payload/TRX.app/LaunchScreen.storyboardc")
    shutil.copy("../data/trx/mac/icon.icns", "Payload/TRX.app/icon.icns")
    shutil.copy("../data/trx/ship/cfg", "Payload/TRX.app/cfg")

def create_ipa():
    subprocess.run(["zip", "-r", "TRX.ipa", "Payload"])

def main():
    create_app()
    copy_files()
    create_ipa()

if __name__ == "__main__":
    main()