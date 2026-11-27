#!/usr/bin/env python3
"""Build the code generator used by tools/codegen.ps1: the LoS1-nx SDK fork at a fixed commit plus patches/los1-nx-generator.patch.

Result: out/generator/los1-nx/sdk/out/win-amd64/rexglue.exe (Windows, llvm-mingw clang). From the repository root:
    python tools/prepare_generator.py --llvm-mingw C:/path/llvm-mingw/bin --ninja C:/path/ninja.exe [--cmake cmake] [--jobs 6]
Without --llvm-mingw it only prepares the sources (clone, patch, third-party fetch).
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
URL = "https://github.com/FurryLilManChild/LoS1-nx.git"
COMMIT = "3426a45875a4fa5049f8c472e0d20c362fa84c64"


def run(*args, cwd=None):
    print(">", *map(str, args))
    subprocess.run([*map(str, args)], cwd=cwd, check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--llvm-mingw", help="llvm-mingw bin folder (clang, clang++)")
    ap.add_argument("--ninja", default="ninja")
    ap.add_argument("--cmake", default="cmake")
    ap.add_argument("--jobs", default="6")
    a = ap.parse_args()
    work = ROOT / "out" / "generator"
    repo = work / "los1-nx"
    sdk = repo / "sdk"
    if not (work / ".sources-prepared").is_file():
        if work.exists():
            shutil.rmtree(work)
        work.mkdir(parents=True)
        run("git", "init", repo)
        run("git", "remote", "add", "origin", URL, cwd=repo)
        run("git", "fetch", "--depth", "1", "origin", COMMIT, cwd=repo)
        run("git", "checkout", "--detach", "FETCH_HEAD", cwd=repo)
        run("git", "apply", "--ignore-space-change", "--directory=sdk", ROOT / "patches/los1-nx-generator.patch", cwd=repo)
        # sdk/thirdparty only holds the files the fork changed; the rest comes from ReXGlue SDK v0.10.0 and its submodules.
        run(sys.executable, "-I", "tools/fetch_thirdparty.py", cwd=repo)
        (work / ".sources-prepared").write_text(COMMIT + "\n", encoding="ascii")
    if not a.llvm_mingw:
        print(f"sources ready in {sdk}")
        return
    llvm = Path(a.llvm_mingw)
    build = work / "b"
    run(a.cmake, "-S", sdk, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
        f"-DCMAKE_C_COMPILER={(llvm / 'clang.exe').as_posix()}", f"-DCMAKE_CXX_COMPILER={(llvm / 'clang++.exe').as_posix()}",
        f"-DCMAKE_MAKE_PROGRAM={Path(a.ninja).as_posix()}", "-DCMAKE_CXX_STANDARD=23",
        "-DCMAKE_C_FLAGS=-march=x86-64-v2", "-DCMAKE_CXX_FLAGS=-march=x86-64-v2",
        "-DREXGLUE_ENABLE_TRACY=OFF", "-DREXGLUE_ENABLE_PERF_COUNTERS=OFF")
    run(a.ninja, "-C", build, "-j", a.jobs, "rexglue")
    print(f"built; the generator is sdk/out/win-amd64/rexglue.exe under {repo} (put its folder and {llvm} on PATH)")


if __name__ == "__main__":
    main()
