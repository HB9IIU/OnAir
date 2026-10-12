"""Build Windows packages and upload a draft release. Requires MSYS2 and gh.

Run from PowerShell: python tools/package/release_windows.py
Commit and push your changes first. Use --build-only to package local changes.
"""

import argparse
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def run(args, *, capture=False, env=None):
    result = subprocess.run(args, cwd=ROOT, env=env, check=True,
                            text=True, stdout=subprocess.PIPE if capture else None)
    return result.stdout.strip() if capture else ""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-only", action="store_true",
                        help="Build packages without accessing GitHub; allows local changes")
    parser.add_argument("--msys2", type=Path, default=Path("C:/msys64"))
    parser.add_argument("--notes", type=Path, help="Use a Markdown release-notes file")
    args = parser.parse_args()
    if sys.platform != "win32":
        parser.error("Run this script on Windows.")
    bash = args.msys2 / "usr/bin/bash.exe"
    if not bash.is_file():
        parser.error(f"MSYS2 bash not found: {bash}")
    match = re.search(r"project\(OnAir VERSION ([0-9.]+)",
                      (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
    if not match:
        parser.error("Cannot read the version from CMakeLists.txt")
    version = match.group(1)
    tag = f"qo100-v{version}"
    repo = commit = None
    if not args.build_only:
        if not shutil.which("gh"):
            parser.error("GitHub CLI (gh) is required. Install it and run gh auth login.")
        run(["gh", "auth", "status"])
        if run(["git", "status", "--porcelain", "--untracked-files=all"], capture=True):
            parser.error("Commit and push your changes first, or use --build-only.")
        remote = run(["git", "remote", "get-url", "origin"], capture=True)
        found = re.fullmatch(r"(?:https://github\.com/|git@github\.com:)([^/]+/[^/]+?)(?:\.git)?/?", remote)
        if not found:
            parser.error("origin must point to a GitHub repository.")
        repo = found.group(1)
        branch = run(["git", "symbolic-ref", "--short", "HEAD"], capture=True)
        commit = run(["git", "rev-parse", "HEAD"], capture=True)
        advertised = run(["git", "ls-remote", "origin", f"refs/heads/{branch}"], capture=True)
        if not advertised or advertised.split()[0] != commit:
            parser.error(f"Push branch {branch} to origin first so the package matches GitHub.")
        pages = json.loads(run(["gh", "api", f"repos/{repo}/releases", "--paginate", "--slurp"], capture=True))
        if any(release["tag_name"] == tag for page in pages for release in page):
            parser.error(f"Release {tag} already exists. Increase the CMakeLists.txt version first.")
        if run(["git", "ls-remote", "origin", f"refs/tags/{tag}"], capture=True):
            parser.error(f"Tag {tag} already exists. Increase the CMakeLists.txt version first.")
    notes = args.notes.read_text(encoding="utf-8") if args.notes else (
        f"OnAir for OSCAR-100 DATV — Windows x64 ({version})\n\n"
        "Fork of https://github.com/adswill/OnAir for the QO-100 DATV community.\n\n"
        "- Live BATC wideband spectrum and click-to-tune.\n"
        "- Adjustable LNB local oscillator frequency and symbol-rate estimation.\n"
        "- Automatic playback and sidebar navigation shortcuts.\n\n"
        "Download the portable ZIP, extract it, and run OnAir.exe. "
        "Alternatively, use the setup EXE if attached. Windows x64 with AVX2 is required. "
        "The BATC spectrum requires an internet connection.\n"
    )
    env = os.environ.copy()
    env["MSYSTEM"] = "UCRT64"
    env["CHERE_INVOKING"] = "1"
    command = f"cd -- {shlex.quote(ROOT.as_posix())} && bash tools/package/make_windows.sh {shlex.quote(version)}"
    run([str(bash), "-lc", command], env=env)
    output = ROOT / "build-windows"
    zip_file = output / f"OnAir-{version}-windows-x64.zip"
    if not zip_file.is_file():
        raise RuntimeError(f"Packaging did not produce {zip_file}")
    assets = [zip_file]
    installer = output / f"OnAir-{version}-windows-x64-setup.exe"
    # Include the installer only if this run generated it.
    if installer.is_file() and installer.stat().st_mtime >= zip_file.stat().st_mtime:
        assets.append(installer)
    for asset in assets:
        print(f"Package: {asset}")
    if args.build_only:
        return
    with tempfile.TemporaryDirectory(prefix="onair-release-") as temporary:
        notes_path = Path(temporary) / "notes.md"
        notes_path.write_text(notes, encoding="utf-8")
        run(["gh", "release", "create", tag, *map(str, assets),
             "--repo", repo, "--target", commit, "--draft",
             "--title", f"OnAir QO-100 {version} — Windows",
             "--notes-file", str(notes_path)])
    print("Draft uploaded. Test the packaged app, then publish the draft on GitHub.")


if __name__ == "__main__":
    try:
        main()
    except (subprocess.CalledProcessError, OSError, RuntimeError) as error:
        print(f"Release stopped: {error}", file=sys.stderr)
        sys.exit(1)
