#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Update an apt-installed Relay from its GitHub releases.

Run by /update (RelayWindow::updateApp) and on its own:

    relay-update.py check                  print whether a newer release exists
    relay-update.py install [--dry-run]    download and verify the .deb for this
                                           distribution and architecture; without
                                           --dry-run, install it (pkexec asks for
                                           the password) and print the marker
    relay-update.py install --restart      as above, then launch the new relay

One short line per step, flushed as it happens: the app shows them as its notice.
The last line is ``CURRENT <tag>`` or ``UPDATED <tag>`` — the marker the app
restarts itself on. Nothing is ever read from stdin; root is pkexec's dialog, or
the printed sudo command when pkexec is missing.

A source checkout (``cmake --install``, ``./build/relay``) is refused rather than
upgraded: the .deb installs into /usr and would shadow it.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

REPO = "elliottash/relay-terminal"
RELEASES_URL = f"https://api.github.com/repos/{REPO}/releases?per_page=30"
DOWNLOAD_URL = f"https://github.com/{REPO}/releases/download/"
USER_AGENT = f"relay-update (+https://github.com/{REPO})"
TIMEOUT = 30

# /etc/os-release → the distribution tag in the release asset names. The release
# workflow builds ubuntu24.04, ubuntu26.04 and debian13 (docs/RELEASING.md);
# anything else has no package to offer.
DISTRO_SLUGS = {
    ("ubuntu", "24.04"): "ubuntu24.04",
    ("ubuntu", "26.04"): "ubuntu26.04",
    ("debian", "13"): "debian13",
}
DPKG_ARCHS = {"x86_64": "amd64", "aarch64": "arm64", "i686": "i386", "i386": "i386"}


def say(text: str) -> None:
    print(text, flush=True)


def distro_slug(os_release: str) -> str | None:
    """The asset-name distribution tag for an /etc/os-release contents."""
    fields = {}
    for line in os_release.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            fields[key.strip()] = value.strip().strip('"')
    return DISTRO_SLUGS.get((fields.get("ID", ""), fields.get("VERSION_ID", "")))


def deb_arch(machine: str) -> str:
    """uname -m → the dpkg architecture used in the asset names."""
    return DPKG_ARCHS.get(machine, machine)


def upstream_version(tag: str) -> str:
    """v0.1.0-beta.3 → 0.1.0~beta.3; v0.1.0 → 0.1.0 (dpkg sorts ~ before the final)."""
    version = tag[1:] if tag.startswith("v") else tag
    head, _, suffix = version.partition("-")
    return head + ("~" + suffix if suffix else "")


def full_version(tag: str, slug: str) -> str:
    """The .deb's Version: line for a tag on this distribution (packaging/cpack.cmake)."""
    return f"{upstream_version(tag)}-1~{slug}"


def asset_name(tag: str, slug: str, arch: str) -> str:
    """The release asset for a tag on one distribution/architecture."""
    version = tag[1:] if tag.startswith("v") else tag
    return f"relay_{version}_{slug}_{arch}.deb"


def read_os_release(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except OSError:
        return ""


def installed_deb_version(package: str = "relay") -> str | None:
    """The Version: of the installed package, or None when dpkg does not know it."""
    try:
        out = subprocess.run(
            ["dpkg-query", "-W", "-f=${Version}", package],
            capture_output=True, text=True, timeout=15, check=True,
        ).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    version = out.strip()
    return version or None


def apt_newer(installed: str, candidate: str) -> bool:
    """dpkg's own ordering: is the candidate strictly newer than what is installed?"""
    result = subprocess.run(
        ["dpkg", "--compare-versions", installed, "lt", candidate],
        capture_output=True, timeout=15,
    )
    return result.returncode == 0


def latest_release() -> dict:
    """The newest non-draft release from GitHub (betas included; they are the channel)."""
    request = urllib.request.Request(
        RELEASES_URL, headers={"Accept": "application/vnd.github+json", "User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=TIMEOUT) as response:
        releases = json.load(response)
    for release in releases:
        if not release.get("draft"):
            return release
    raise RuntimeError("GitHub returned no published release.")


def download(url: str, destination: Path) -> None:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=TIMEOUT) as response, destination.open("wb") as out:
        shutil.copyfileobj(response, out)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def checksums_asset(release: dict) -> dict | None:
    return next((a for a in release.get("assets", []) if a.get("name") == "SHA256SUMS"), None)


def verified_checksum(sums: str, name: str) -> str | None:
    for line in sums.splitlines():
        fields = line.split()
        if len(fields) == 2 and fields[1] == name:
            return fields[0]
    return None


def root_installer(deb: Path) -> tuple[list[str], str] | None:
    """(command, description) that installs the package as root, or None with the
    sudo line already printed for the user to run by hand."""
    pkexec = shutil.which("pkexec")
    apt_get = shutil.which("apt-get")
    if pkexec and apt_get:
        return [pkexec, os.path.abspath(apt_get), "install", "-y", "--", str(deb)], "pkexec"
    if apt_get:
        say(f"Root is needed to install it. Run: sudo apt install {deb}")
    else:
        say(f"This machine has no apt; the package is at {deb}")
    return None


def install_command(packages_slug: str | None, machine: str, dry_run: bool, restart: bool) -> int:
    slug = packages_slug or distro_slug(read_os_release(Path("/etc/os-release")))
    if not slug:
        say(f"No release package for this distribution (built: {', '.join(sorted(set(DISTRO_SLUGS.values())))}).")
        return 1
    arch = deb_arch(machine)
    installed = installed_deb_version()
    if installed is None:
        say("This Relay was not installed from a package — /update upgrades the .deb install.")
        return 1

    say("Checking GitHub for the latest release…")
    try:
        release = latest_release()
    except Exception as error:  # a line the notice can show, not a traceback
        say(f"Could not reach GitHub: {error}")
        return 1
    tag = release.get("tag_name", "")
    candidate = full_version(tag, slug)
    if not apt_newer(installed, candidate):
        say(f"Relay {installed} is already the latest release.")
        say(f"CURRENT {tag}")
        return 0

    name = asset_name(tag, slug, arch)
    asset = next((a for a in release.get("assets", []) if a.get("name") == name), None)
    if asset is None:
        available = ", ".join(sorted(a.get("name", "") for a in release.get("assets", []))[:8])
        say(f"The latest release has no package for {slug} {arch}.")
        say(f"It published: {available}")
        return 1

    work = Path(tempfile.mkdtemp(prefix="relay-update-"))
    deb = work / name
    try:
        megabytes = max(asset.get("size", 0), 1) / (1 << 20)
        say(f"Downloading {name} ({megabytes:.1f} MB)…")
        try:
            download(asset["browser_download_url"], deb)
        except Exception as error:
            say(f"Download failed: {error}")
            return 1

        sums = checksums_asset(release)
        if sums is None:
            say("The release has no SHA256SUMS asset; refusing to install unverified.")
            return 1
        sums_path = work / "SHA256SUMS"
        try:
            download(sums["browser_download_url"], sums_path)
        except Exception as error:
            say(f"Could not download SHA256SUMS: {error}")
            return 1
        expected = verified_checksum(sums_path.read_text(encoding="utf-8"), name)
        if expected is None:
            say(f"SHA256SUMS does not list {name}.")
            return 1
        actual = sha256(deb)
        if actual != expected:
            say("Checksum mismatch — the downloaded package is corrupt. Nothing was installed.")
            return 1
        say("Checksum verified.")

        if dry_run:
            say(f"Dry run: {name} downloaded and verified at {deb} (not installed).")
            return 0

        installer = root_installer(deb)
        if installer is None:
            return 2
        command, via = installer
        say("Installing (the password dialog is pkexec's)…")
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode != 0:
            detail = (result.stderr or result.stdout or "").strip().splitlines()
            say(f"Install failed ({via}): {detail[-1] if detail else 'unknown error'}.")
            say(f"The verified package is still at {deb}")
            return 1
        now = installed_deb_version() or candidate
        say(f"Installed Relay {now}.")
        if restart:
            relay = shutil.which("relay")
            if relay:
                subprocess.Popen([relay], start_new_session=True,
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                say("Launched the new relay.")
            else:
                say("Could not find a relay on PATH to launch.")
        say(f"UPDATED {tag}")
        return 0
    finally:
        if not dry_run:
            shutil.rmtree(work, ignore_errors=True)


def check_command() -> int:
    installed = installed_deb_version()
    if installed is None:
        say("This Relay was not installed from a package — /update upgrades the .deb install.")
        return 1
    slug = distro_slug(read_os_release(Path("/etc/os-release")))
    if not slug:
        say(f"No release package for this distribution (built: {', '.join(sorted(set(DISTRO_SLUGS.values())))}).")
        return 1
    try:
        release = latest_release()
    except Exception as error:
        say(f"Could not reach GitHub: {error}")
        return 1
    tag = release.get("tag_name", "")
    if apt_newer(installed, full_version(tag, slug)):
        say(f"Update available: {tag} (installed: {installed}).")
        say(f"AVAILABLE {tag}")
    else:
        say(f"Relay {installed} is already the latest release.")
        say(f"CURRENT {tag}")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Update an apt-installed Relay from GitHub releases.")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("check", help="print whether a newer release exists")
    install = commands.add_parser("install", help="download, verify and install the latest release")
    install.add_argument("--dry-run", action="store_true", help="download and verify, but do not install")
    install.add_argument("--restart", action="store_true", help="launch relay after installing")
    args = parser.parse_args(argv)

    try:
        if args.command == "check":
            return check_command()
        return install_command(None, os.uname().machine, args.dry_run, args.restart)
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
