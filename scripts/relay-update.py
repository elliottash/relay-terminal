#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Update an apt-installed Relay from its GitHub releases.

Run by /update (RelayWindow::updateApp) and on its own:

    relay-update.py check                  print whether a newer release exists
    relay-update.py install [--dry-run]    download and verify the .deb for this
                                           distribution and architecture; without
                                           --dry-run, install it (pkexec asks for
                                           the password) and print the marker
    relay-update.py install --restart      as above, then launch the new relay
    ... --channel all|stable               which releases to offer: every published
                                           release, betas included (the default), or
                                           only the ones GitHub does not mark as a
                                           prerelease. Relay passes the Update channel
                                           option here.

Within the chosen channel the **highest** version wins, in dpkg's ordering — GitHub
lists releases by creation date, so a patch cut for an older tag is listed first.

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

# Which releases /update offers (owner decision, 2026-09-19). The default is what the script always
# did — every published release, betas included — and "stable" leaves the prereleases out.
CHANNELS = ("all", "stable")
CHANNEL_DEFAULT = "all"
CHANNEL_EMPTY = {"all": "GitHub returned no published release.",
                 "stable": "GitHub has no stable release yet (every published release is a prerelease). "
                           "Set the update channel to \"all\" to follow the betas."}


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


def apt_newer(installed: str, candidate: str) -> bool | None:
    """dpkg's own ordering: is the candidate strictly newer than what is installed?

    None when dpkg could not answer. `dpkg --compare-versions` exits 0 for true and 1 for false,
    but **2** for a version string it cannot parse — an empty release tag makes the candidate
    `-1~ubuntu24.04` — and reading that as "false" is how a broken tag came out as "already the
    latest release", which is the one answer that must never be a guess.
    """
    if not installed.strip() or not candidate.strip():
        return None
    try:
        result = subprocess.run(
            ["dpkg", "--compare-versions", installed, "lt", candidate],
            capture_output=True, timeout=15,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if result.returncode in (0, 1):
        return result.returncode == 0
    return None


def release_tag(release: dict) -> str:
    return str(release.get("tag_name") or "")


def in_channel(release: dict, channel: str) -> bool:
    """Whether a release belongs to the chosen channel.

    A draft is never offered: it has no public assets. ``all`` is the default channel and takes
    every published release, prereleases included (the beta tags are what a preview user is
    following). ``stable`` takes only the releases GitHub does not mark as a prerelease —
    `gh release create --prerelease` is set for every tag with a `-` in it (docs/RELEASING.md), so
    the flag and the tilde in the .deb version say the same thing.
    """
    if release.get("draft"):
        return False
    return not (channel == "stable" and release.get("prerelease"))


def comparable_version(version: str) -> bool:
    """Whether dpkg can read this version string at all (`apt_newer` answers None when it cannot)."""
    return apt_newer(version, version) is not None


def pick_release(releases, channel: str = CHANNEL_DEFAULT) -> dict | None:
    """The highest-version release in `channel`, or None when the channel is empty.

    GitHub lists releases in created-at order, so the first one is **not** the highest version: a
    patch cut for an older tag is created last and was offered as "latest" (the dpkg comparison
    below then refused it as a downgrade, which is right but reads as "already the latest
    release"). So every candidate is compared with the same `dpkg --compare-versions` the rest of
    this script uses, on the upstream version alone — the `-1~<slug>` suffix is the same for all of
    them on one machine, and leaving it off keeps this decision independent of the distribution.

    A tag dpkg cannot parse takes no part in the ordering, because there is no answer to guess:
    it is skipped while any readable tag is in the channel, and only when none is does GitHub's
    own order decide, exactly as it did before. The caller still compares what comes back with the
    installed version and refuses an unreadable one there.
    """
    candidates = [r for r in releases if in_channel(r, channel)]
    if not candidates:
        return None
    readable = [r for r in candidates if comparable_version(upstream_version(release_tag(r)))]
    pool = readable or candidates
    best = pool[0]
    for release in pool[1:]:
        if apt_newer(upstream_version(release_tag(best)), upstream_version(release_tag(release))):
            best = release
    return best


def fetch_releases() -> list:
    request = urllib.request.Request(
        RELEASES_URL, headers={"Accept": "application/vnd.github+json", "User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=TIMEOUT) as response:
        releases = json.load(response)
    return releases if isinstance(releases, list) else []


def latest_release(channel: str = CHANNEL_DEFAULT) -> dict:
    """The highest-version release in `channel` (see `pick_release`)."""
    release = pick_release(fetch_releases(), channel)
    if release is None:
        raise RuntimeError(CHANNEL_EMPTY[channel])
    return release


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


def install_command(packages_slug: str | None, machine: str, dry_run: bool, restart: bool,
                    channel: str = CHANNEL_DEFAULT) -> int:
    slug = packages_slug or distro_slug(read_os_release(Path("/etc/os-release")))
    if not slug:
        say(f"No release package for this distribution (built: {', '.join(sorted(set(DISTRO_SLUGS.values())))}).")
        return 1
    arch = deb_arch(machine)
    installed = installed_deb_version()
    if installed is None:
        say("This Relay was not installed from a package — /update upgrades the .deb install.")
        return 1

    say("Checking GitHub for the latest release…" if channel == "all"
        else "Checking GitHub for the latest stable release…")
    try:
        release = latest_release(channel)
    except Exception as error:  # a line the notice can show, not a traceback
        say(f"Could not reach GitHub: {error}")
        return 1
    tag = release_tag(release)
    if not tag.strip():
        say("The latest GitHub release has no tag; nothing to install.")
        return 1
    candidate = full_version(tag, slug)
    newer = apt_newer(installed, candidate)
    if newer is None:
        say(f"Could not compare {installed} with {candidate}; nothing was installed.")
        return 1
    if not newer:
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
    #: True once a line has told the user where the .deb is. Deleting it after that made every
    #: such line a lie — "Run: sudo apt install <path>" named a file this function had just
    #: removed — so the download is kept exactly when its path has been printed.
    keep = dry_run
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
            keep = True                 # it printed the sudo line, or where the package is
            return 2
        command, via = installer
        say("Installing (the password dialog is pkexec's)…")
        # No timeout: the first thing this waits on is polkit's password dialog, and there is no
        # sane number of seconds to give a person typing a password. Without a session to show it
        # in, pkexec fails immediately, and apt's own lock wait is bounded.
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode != 0:
            detail = (result.stderr or result.stdout or "").strip().splitlines()
            say(f"Install failed ({via}): {detail[-1] if detail else 'unknown error'}.")
            say(f"The verified package is still at {deb}")
            keep = True
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
        if not keep:
            shutil.rmtree(work, ignore_errors=True)


def check_command(channel: str = CHANNEL_DEFAULT) -> int:
    installed = installed_deb_version()
    if installed is None:
        say("This Relay was not installed from a package — /update upgrades the .deb install.")
        return 1
    slug = distro_slug(read_os_release(Path("/etc/os-release")))
    if not slug:
        say(f"No release package for this distribution (built: {', '.join(sorted(set(DISTRO_SLUGS.values())))}).")
        return 1
    try:
        release = latest_release(channel)
    except Exception as error:
        say(f"Could not reach GitHub: {error}")
        return 1
    tag = release_tag(release)
    if not tag.strip():
        say("The latest GitHub release has no tag.")
        return 1
    newer = apt_newer(installed, full_version(tag, slug))
    if newer is None:
        say(f"Could not compare {installed} with {full_version(tag, slug)}.")
        return 1
    if newer:
        say(f"Update available: {tag} (installed: {installed}).")
        say(f"AVAILABLE {tag}")
    else:
        say(f"Relay {installed} is already the latest release.")
        say(f"CURRENT {tag}")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Update an apt-installed Relay from GitHub releases.")
    commands = parser.add_subparsers(dest="command", required=True)
    channel = argparse.ArgumentParser(add_help=False)
    channel.add_argument("--channel", choices=CHANNELS, default=CHANNEL_DEFAULT,
                         help='which releases to offer: "all" (the default: every published '
                              'release, betas included) or "stable" (no prereleases)')
    commands.add_parser("check", parents=[channel], help="print whether a newer release exists")
    install = commands.add_parser("install", parents=[channel],
                                  help="download, verify and install the latest release")
    install.add_argument("--dry-run", action="store_true", help="download and verify, but do not install")
    install.add_argument("--restart", action="store_true", help="launch relay after installing")
    args = parser.parse_args(argv)

    try:
        if args.command == "check":
            return check_command(args.channel)
        return install_command(None, os.uname().machine, args.dry_run, args.restart, args.channel)
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
