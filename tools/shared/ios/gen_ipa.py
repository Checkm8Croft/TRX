#!/usr/bin/env python3
import os
import subprocess
import shutil
from pathlib import Path

def copy_item(src, dst):
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
    # Lavoriamo nella cartella di build
    os.chdir("build-ios-arm64")
    
    # Ricreiamo la cartella Payload da zero
    if os.path.exists("Payload"):
        shutil.rmtree("Payload")
    os.makedirs("Payload/TRX.app")

def copy_files():
    app_dir = "Payload/TRX.app"
    
    # Copia file ed eventuali directory
    copy_item("TRX", f"{app_dir}/TRX")
    copy_item("../tools/shared/ios/Info.plist", f"{app_dir}/Info.plist")
    copy_item("../tools/shared/ios/LaunchScreen.storyboardc", f"{app_dir}/LaunchScreen.storyboardc")
    copy_item("../data/trx/mac/icon.icns", f"{app_dir}/icon.icns")
    copy_item("../data/trx/ship/cfg", f"{app_dir}/cfg")

    # Assicura i permessi di esecuzione sull'eseguibile principale
    os.chmod(f"{app_dir}/TRX", 0o755)

def create_ipa():
    # Genera il file .ipa zippando la cartella Payload
    subprocess.run(["zip", "-r", "TRX.ipa", "Payload"], check=True)
    print("✅ TRX.ipa generated!")

def main():
    create_app()
    copy_files()
    create_ipa()

if __name__ == "__main__":
    main()
