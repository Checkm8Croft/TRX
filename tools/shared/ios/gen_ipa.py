#!/usr/bin/env python3
import os
import shutil
import sys
import zipfile
from pathlib import Path

# Optional first argument: the build directory to package (a sibling of
# tools/, as the relative paths below assume). Defaults to the debug build.
BUILD_DIR = sys.argv[1] if len(sys.argv) > 1 else "build-ios-arm64"

def copy_item(src, dst):
    """Copia sia file che directory in modo sicuro."""
    src_path = Path(src)
    dst_path = Path(dst)

    if not src_path.exists():
        print(f"⚠️ Warning: {src} doesn't exsist.")
        return

    if src_path.is_dir():
        if dst_path.exists():
            shutil.rmtree(dst_path)
        shutil.copytree(src_path, dst_path)
    else:
        dst_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src_path, dst_path)

def create_app():
    os.chdir(BUILD_DIR)
    
    if os.path.exists("Payload"):
        shutil.rmtree("Payload")
    os.makedirs("Payload/TRX.app")

def copy_files():
    app_dir = "Payload/TRX.app"
    copy_item("TRX", f"{app_dir}/TRX")
    copy_item("../tools/shared/ios/Info.plist", f"{app_dir}/Info.plist")
    copy_item("../tools/shared/ios/LaunchScreen.storyboardc", f"{app_dir}/LaunchScreen.storyboardc")
    copy_item("../data/trx/icon.png", f"{app_dir}/icon.png")
    copy_item("../data/trx/ship/cfg", f"{app_dir}/cfg")
    copy_item("../data/trx/ship/modules", f"{app_dir}/modules")
    copy_item("../data/trx/ship/scripts", f"{app_dir}/scripts")

    if os.path.exists(f"{app_dir}/TRX"):
        os.chmod(f"{app_dir}/TRX", 0o755)

def create_ipa():
    ipa_filename = "TRX.ipa"
    print("📦 Creating IPA...")
    
    with zipfile.ZipFile(ipa_filename, 'w', zipfile.ZIP_DEFLATED) as ipa_zip:
        for root, dirs, files in os.walk("Payload"):
            for file in files:
                file_path = os.path.join(root, file)
                # Mantiene la struttura interna partendo da Payload/...
                ipa_zip.write(file_path, file_path)

    print(f"✅ {ipa_filename} generated!")

def main():
    create_app()
    copy_files()
    create_ipa()

if __name__ == "__main__":
    main()
