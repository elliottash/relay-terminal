# SPDX-License-Identifier: GPL-3.0-or-later
"""scripts/relay-update.py decides, offline, which package a machine takes.

The script talks to GitHub only at run time; everything that decides the outcome —
which distribution tag an /etc/os-release maps to, which dpkg architecture a uname
is, which Version: a tag becomes, which asset name it publishes as — is a pure
function, and this is what keeps the download aimed at the right file. dpkg's own
--compare-versions does the ordering, so the beta tilde sorts as dpkg sorts it.
"""
import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "scripts" / "relay-update.py"


def load_updater():
    spec = importlib.util.spec_from_file_location("relay_update", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class UpdateScriptTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.updater = load_updater()

    def test_distro_slug_matches_the_release_assets(self):
        ubuntu = 'NAME="Ubuntu"\nVERSION="24.04 LTS"\nID=ubuntu\nVERSION_ID="24.04"\n'
        self.assertEqual(self.updater.distro_slug(ubuntu), "ubuntu24.04")
        trixie = 'PRETTY_NAME="Debian GNU/Linux 13 (trixie)"\nID=debian\nVERSION_ID="13"\n'
        self.assertEqual(self.updater.distro_slug(trixie), "debian13")
        self.assertEqual(self.updater.distro_slug("ID=ubuntu\nVERSION_ID=26.04\n"), "ubuntu26.04")

    def test_distro_slug_refuses_what_has_no_package(self):
        self.assertIsNone(self.updater.distro_slug("ID=fedora\nVERSION_ID=42\n"))
        self.assertIsNone(self.updater.distro_slug("ID=ubuntu\n"))          # no VERSION_ID
        self.assertIsNone(self.updater.distro_slug(""))                     # no os-release at all

    def test_arch_names_match_dpkg(self):
        self.assertEqual(self.updater.deb_arch("x86_64"), "amd64")
        self.assertEqual(self.updater.deb_arch("aarch64"), "arm64")
        self.assertEqual(self.updater.deb_arch("riscv64"), "riscv64")       # passed through

    def test_tag_becomes_the_package_version(self):
        # The ~ keeps a beta below the final in dpkg's ordering (docs/RELEASING.md).
        self.assertEqual(self.updater.upstream_version("v0.1.0-beta.3"), "0.1.0~beta.3")
        self.assertEqual(self.updater.upstream_version("v0.1.0"), "0.1.0")
        self.assertEqual(self.updater.full_version("v0.1.0-beta.3", "ubuntu24.04"),
                         "0.1.0~beta.3-1~ubuntu24.04")
        self.assertEqual(self.updater.full_version("v0.1.0", "debian13"), "0.1.0-1~debian13")

    def test_asset_name_keeps_the_tag_spelling(self):
        # GitHub rewrites "~" in asset names, so the file keeps the dash (release.yml).
        self.assertEqual(self.updater.asset_name("v0.1.0-beta.3", "ubuntu24.04", "amd64"),
                         "relay_0.1.0-beta.3_ubuntu24.04_amd64.deb")
        self.assertEqual(self.updater.asset_name("v0.1.0", "debian13", "arm64"),
                         "relay_0.1.0_debian13_arm64.deb")

    def test_checksum_is_read_for_the_right_file(self):
        sums = ("0f00… relay_0.1.0-beta.3_ubuntu24.04_amd64.deb\n"
                "aa11… relay_0.1.0-beta.3_debian13_arm64.deb\n"
                "bb22… relay-0.1.0-beta.3.tar.gz\n")
        self.assertEqual(self.updater.verified_checksum(sums, "relay_0.1.0-beta.3_debian13_arm64.deb"),
                         "aa11…")
        self.assertIsNone(self.updater.verified_checksum(sums, "relay_0.1.0-beta.3_ubuntu26.04_amd64.deb"))

    def test_dpkg_orders_the_versions_the_script_compares(self):
        older = "0.1.0~beta.2-1~ubuntu24.04"
        newer = "0.1.0~beta.3-1~ubuntu24.04"
        final = "0.1.0-1~ubuntu24.04"
        self.assertTrue(self.updater.apt_newer(older, newer))
        self.assertFalse(self.updater.apt_newer(newer, newer))
        self.assertFalse(self.updater.apt_newer(final, newer))
        self.assertTrue(self.updater.apt_newer(newer, final))

    def test_install_refuses_a_machine_without_the_package(self):
        # A source checkout must be told it is one, not offered a .deb that would shadow it.
        self.assertIsNone(self.updater.installed_deb_version(package="relay-not-a-package"))

    def test_a_version_dpkg_cannot_read_is_not_read_as_older(self):
        # dpkg exits 2 for a version it cannot parse, and reading that as "false" came out as
        # "already the latest release" — the one answer that must never be a guess.
        self.assertIsNone(self.updater.apt_newer("0.1.0-1~ubuntu24.04", "-1~ubuntu24.04"))
        self.assertIsNone(self.updater.apt_newer("", "0.1.0-1~ubuntu24.04"))
        self.assertIsNone(self.updater.apt_newer("0.1.0-1~ubuntu24.04", "   "))


class FailedInstallTest(unittest.TestCase):
    """What the user is told, and whether the file it names is still there.

    `install_command` printed "The verified package is still at <path>" and then deleted the
    directory holding it on the way out; so did the no-pkexec branch's "Run: sudo apt install
    <path>". Every such line has to leave the download behind.
    """

    def setUp(self):
        self.updater = load_updater()
        self.said = []
        self.updater.say = self.said.append
        self.work = []

        release = {"tag_name": "v9.9.9", "draft": False, "assets": [
            {"name": "relay_9.9.9_ubuntu24.04_amd64.deb", "size": 1024,
             "browser_download_url": "https://example.invalid/relay.deb"},
            {"name": "SHA256SUMS", "browser_download_url": "https://example.invalid/SHA256SUMS"}]}
        self.updater.latest_release = lambda: release
        self.updater.installed_deb_version = lambda package="relay": "0.0.1-1~ubuntu24.04"

        def download(url, destination):
            destination.write_bytes(b"a package" if url.endswith(".deb") else
                                    b"x  relay_9.9.9_ubuntu24.04_amd64.deb\n")
            self.work.append(destination.parent)

        self.updater.download = download
        self.updater.sha256 = lambda path: "x"

    def run_install(self):
        return self.updater.install_command("ubuntu24.04", "x86_64", False, False)

    def kept_path(self):
        printed = [line for line in self.said if ".deb" in line]
        self.assertTrue(printed, self.said)
        return Path(printed[-1].split()[-1])

    def test_the_package_outlives_a_failed_install(self):
        self.updater.root_installer = lambda deb: ([("false")], "pkexec")
        self.assertEqual(self.run_install(), 1)
        self.assertIn("still at", " ".join(self.said))
        self.assertTrue(self.kept_path().is_file(), self.said)

    def test_the_package_outlives_a_machine_with_no_pkexec(self):
        self.updater.root_installer = lambda deb: (self.updater.say(
            f"Root is needed to install it. Run: sudo apt install {deb}"), None)[1]
        self.assertEqual(self.run_install(), 2)
        self.assertTrue(self.kept_path().is_file(), self.said)

    def test_a_release_with_no_tag_is_refused(self):
        self.updater.latest_release = lambda: {"tag_name": "", "draft": False, "assets": []}
        self.assertEqual(self.run_install(), 1)
        self.assertIn("no tag", " ".join(self.said))

    def test_a_successful_install_leaves_nothing_behind(self):
        self.updater.root_installer = lambda deb: ([("true")], "pkexec")
        self.assertEqual(self.run_install(), 0)
        self.assertIn("UPDATED v9.9.9", self.said)
        self.assertEqual([d for d in self.work if d.exists()], [])


if __name__ == "__main__":
    unittest.main()
