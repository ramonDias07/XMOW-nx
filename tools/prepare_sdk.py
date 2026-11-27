#!/usr/bin/env python3
"""Fetch the public ReXGlue SDK NX, apply the XMOW-nx patch and prepare its Switch dependencies in sdk/."""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SDK_COMMIT = "f961303fe3c6e3a90905b379ff4bf3de9a35a757"
SDK_URL = "https://github.com/nx-mod/rexglue-sdk-nx.git"


def git(*args, cwd=ROOT):
    subprocess.run(["git", *map(str, args)], cwd=cwd, check=True)


def fix_mspack_links(checkout):
    # Git for Windows can check symbolic links out as one-line text files.
    mspack = checkout / "thirdparty/libmspack"
    folder = mspack / "cabextract/mspack"
    if not folder.is_dir():
        return
    for path in folder.iterdir():
        if path.is_file() and not path.is_symlink() and path.stat().st_size < 256:
            target = path.read_text(encoding="ascii", errors="replace").strip()
            if target.startswith("../../libmspack/mspack/") or target == "../../libmspack/ChangeLog":
                source = (path.parent / target).resolve(strict=True)
                if not source.is_relative_to(mspack):
                    raise ValueError(f"Unexpected libmspack link target for {path.name}")
                shutil.copyfile(source, path)


def prepare_sdk():
    sdk = ROOT / "sdk"
    if (sdk / ".xmow-sdk-prepared").is_file():
        print("sdk/ already prepared")
        return
    if sdk.exists():
        raise SystemExit(f"Existing unprepared SDK directory: {sdk}")
    with tempfile.TemporaryDirectory(dir=ROOT, prefix="xmow-sdk-") as folder:
        checkout = Path(folder) / "sdk"
        git("init", checkout)
        git("remote", "add", "origin", SDK_URL, cwd=checkout)
        git("fetch", "--depth", "1", "origin", SDK_COMMIT, cwd=checkout)
        git("checkout", "--detach", "FETCH_HEAD", cwd=checkout)
        git("apply", "--ignore-space-change", ROOT / "patches/rexglue-nx.patch", cwd=checkout)
        # The SDK's own setup: Switch submodules, SPIRV-Cross and uam at fixed commits, FFmpeg overlay.
        subprocess.run([sys.executable, "tools/switch_setup.py"], cwd=checkout, check=True)
        # uam is fetched by switch_setup.py, so its one XMOW change is a second patch applied after it.
        git("apply", "--ignore-space-change", ROOT / "patches/uam-xmow.patch", cwd=checkout)
        fix_mspack_links(checkout)
        (checkout / ".xmow-sdk-prepared").write_text(SDK_COMMIT + "\n", encoding="ascii")
        checkout.rename(sdk)
    print(f"prepared {sdk}")


if __name__ == "__main__":
    prepare_sdk()
